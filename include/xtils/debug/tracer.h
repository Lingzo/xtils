#pragma once

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <list>
#include <sstream>
#include <string>
#include <vector>

#ifdef TRACE_DISABLED
#define TRACE_SCOPE(name)
#define TRACE_INSTANT(name)
#define TRACE_DATA(p_str)
#define TRACE_SAVE(filename)
#else
#define TRACE_SCOPE(name) TraceScopeRAII __trace_scope_raii_##__LINE__(name)
#define TRACE_INSTANT(name) Tracer::instance().recordInstant(name)
#define TRACE_DATA(p_str) Tracer::instance().data(p_str)
#define TRACE_SAVE(filename) Tracer::instance().save(filename)
#endif

class Tracer {
 public:
  struct KV {
    std::string name;
    std::string value;
  };
  struct Event {
    const char* name;
    const char* phase;
    uint64_t ts_us;
    uint64_t dur_us;
    uint32_t pid;
    uint32_t tid;
    uint32_t cpu[2];
    std::list<KV> args;

    std::string toJSON() const {
      char buffer[512] = {0};
      int len = snprintf(buffer, sizeof(buffer),
                         R"({"name":"%s","cat":"function","ph":"%s","ts":%lu)",
                         name, phase, ts_us);
      if (phase[0] == 'X') {
        len += snprintf(buffer + len, sizeof(buffer) - len, R"(,"dur":%lu)",
                        dur_us);
      }
      len += snprintf(buffer + len, sizeof(buffer) - len,
                      R"(,"pid":%u,"tid":%u)", pid, tid);

      // args
      std::stringstream ss;
      ss << "{";
      for (const auto& kv : args) {
        ss << "\"" << kv.name << "\":\"" << kv.value << "\",";
      }
      ss << "\"cpu\":" << cpu[0] << ",";
      ss << "\"cpu2\":" << cpu[1];
      ss << "}";
      std::string args_str = ss.str();
      if (!args_str.empty()) {
        len += snprintf(buffer + len, sizeof(buffer) - len, R"(,"args":%s})",
                        args_str.c_str());
      } else {
        len += snprintf(buffer + len, sizeof(buffer) - len, "}");
      }
      return std::string(buffer);
    }
  };

  static constexpr size_t MAX_EVENTS = 1e4;

  Tracer() : write_index_(0) { events_ = new Event[MAX_EVENTS]; }

  ~Tracer() { delete[] events_; }

  static Tracer& instance() {
    static Tracer inst;
    return inst;
  }

  void recordInstant(const char* name) {
    registerThreadName();
    Event e{name, "i", now_us(), 0, pid(), tid(), {cpu(), 0}};
    pushEvent(e);
  }

  void begin(const char* name) {
    registerThreadName();
    uint64_t ts = now_us();
    ThreadLocalData& data = tls();
    data.stack.push_back({name, "X", ts, 0, pid(), tid(), {cpu(), 0}});
  }

  void end() {
    uint64_t ts = now_us();
    ThreadLocalData& data = tls();
    if (data.stack.empty()) return;
    Event e = data.stack.back();
    data.stack.pop_back();
    e.dur_us = ts - e.ts_us;
    e.cpu[1] = cpu();  // end cpu
    pushEvent(e);
  }

  void data(std::string* out) {
    size_t snapshot = write_index_.load(std::memory_order_acquire);
    size_t count = full_ ? MAX_EVENTS : snapshot;
    size_t start = full_ ? snapshot % MAX_EVENTS : 0;

    std::stringstream ss;
    ss << "{\"traceEvents\":[\n";
    for (size_t i = 0; i < count; ++i) {
      size_t idx = (start + i) % MAX_EVENTS;
      ss << events_[idx].toJSON();
      if (i != count - 1) ss << ",";
      ss << "\n";
    }
    ss << "]}\n";
    *out = ss.str();
  }

  void save(const std::string& filename) {
    std::ofstream out(filename);
    std::string str;
    data(&str);
    out << str;
    out.close();
  }
  void registerThreadName() {
    ThreadLocalData& data = tls();
    if (data.thread_metadata_registered) return;

    data.thread_metadata_registered = true;

    uint32_t thread_id = tid();
    char name[32];
    pthread_getname_np(pthread_self(), name, sizeof(name));
    std::string thread_name(name);
    Event meta;
    meta.name = "thread_name";
    meta.phase = "M";
    meta.ts_us = 0;
    meta.dur_us = 0;
    meta.pid = pid();
    meta.tid = thread_id;
    meta.args.push_back({"name", thread_name});

    pushEvent(meta);
  }

 private:
  struct ThreadLocalData {
    std::vector<Event> stack;
    bool thread_metadata_registered = false;
  };

  static ThreadLocalData& tls() {
    thread_local ThreadLocalData data;
    return data;
  }

  uint64_t now_us() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
               now.time_since_epoch())
        .count();
  }
  uint32_t cpu() const { return static_cast<uint32_t>(sched_getcpu()); }
  uint32_t tid() const { return gettid(); }
  uint32_t pid() const {
    static uint32_t cached_pid = static_cast<uint32_t>(getpid());
    return cached_pid;
  }
  void pushEvent(const Event& e) {
    size_t idx = write_index_.fetch_add(1, std::memory_order_relaxed);
    if (idx >= MAX_EVENTS) {
      full_ = true;
      if (idx % MAX_EVENTS == 0) {
        tls().thread_metadata_registered = false;
      }
    }
    events_[idx % MAX_EVENTS] = e;
  }

  std::atomic<size_t> write_index_;
  Event* events_;
  bool full_;
};

// RAII 范围事件
class TraceScopeRAII {
 public:
  TraceScopeRAII(const char* name) { Tracer::instance().begin(name); }
  ~TraceScopeRAII() { Tracer::instance().end(); }
};
