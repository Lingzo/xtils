#pragma once

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <forward_list>
#include <fstream>
#include <string>

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
    const char *name;
    const char *phase;
    uint64_t ts_us;
    uint64_t dur_us;
    uint32_t pid;
    uint32_t tid;
    uint32_t cpu;
    uint32_t cpu2;
    std::forward_list<KV> args;

    std::string toJSON() const {
      std::string json;
      json.reserve(512);
      auto format_str = [](const char *fmt, ...) {
        char buffer[128] = {0};
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        return std::string(buffer);
      };

      json.append(
          format_str(R"({"name":"%s","cat":"function","ph":"%s","ts":%lu)",
                     name, phase, ts_us));
      if (phase[0] == 'X') {
        json.append(format_str(R"(,"dur":%lu)", dur_us));
      }
      json.append(format_str(R"(,"pid":%u,"tid":%u)", pid, tid));

      // args
      std::string args_str;
      auto append = [&](const std::string &str) { args_str.append(str); };
      for (const auto &kv : args) {
        append(format_str(R"("%s":"%s",)", kv.name.c_str(), kv.value.c_str()));
      }
      if (phase[0] == 'X') {
        append(format_str(R"("cpu":%d,)", cpu));
        append(format_str(R"("cpu2":%d)", cpu2));
      } else if (phase[0] == 'i') {
        append(format_str(R"("cpu":%d)", cpu));
      } else if (!args_str.empty()) {
        args_str.pop_back();
      }
      if (!args_str.empty()) {
        json.append(format_str(R"(,"args":{%s})", args_str.c_str()));
      }
      json.append("}");
      return json;
    }
  };

  static constexpr size_t MAX_EVENTS = 1e4;

  Tracer() : write_index_(0) { events_ = new Event[MAX_EVENTS]; }

  ~Tracer() { delete[] events_; }

  static Tracer &instance() {
    static Tracer inst;
    return inst;
  }

  void recordInstant(const char *name) {
    registerThreadName();
    Event e{name, "i", now_us(), 0, pid(), tid(), cpu()};
    pushEvent(e);
  }

  void data(std::string *out) {
    size_t snapshot = write_index_.load(std::memory_order_acquire);
    size_t count = full_ ? MAX_EVENTS : snapshot;
    size_t start = full_ ? snapshot % MAX_EVENTS : 0;

    out->clear();
    out->reserve(count * 512);
    auto append = [&](const std::string &str) { out->append(str); };
    append("{\"traceEvents\":[\n");
    for (size_t i = 0; i < count; ++i) {
      size_t idx = (start + i) % MAX_EVENTS;
      append(events_[idx].toJSON());
      if (i != count - 1) append(",");
      append("\n");
    }
    append("]}\n");
  }

  void save(const std::string &filename) {
    std::ofstream out(filename);
    std::string str;
    data(&str);
    out << str;
    out.close();
  }
  void registerThreadName() {
    ThreadLocalData &data = tls();
    size_t snapshot = write_index_.load(std::memory_order_acquire);
    if (snapshot - data.meta_idx < MAX_EVENTS && data.meta_idx != 0) return;

    data.meta_idx = snapshot;

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
    meta.args.push_front({"name", thread_name});

    pushEvent(meta);
  }

 private:
  friend class TraceScopeRAII;
  void begin(const char *name) {
    ThreadLocalData &data = tls();
    registerThreadName();
    uint64_t ts = now_us();
    data.stack.push_front({name, "X", ts, 0, pid(), tid(), cpu()});
  }

  void end() {
    uint64_t ts = now_us();
    ThreadLocalData &data = tls();
    if (data.stack.empty()) return;
    Event e = data.stack.front();
    data.stack.pop_front();
    e.dur_us = ts - e.ts_us;
    e.cpu2 = cpu();  // end cpu
    pushEvent(e);
  }

 private:
  struct ThreadLocalData {
    std::forward_list<Event> stack;
    size_t meta_idx = 0;
  };

  static ThreadLocalData &tls() {
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
  void pushEvent(const Event &e) {
    size_t idx = write_index_.fetch_add(1, std::memory_order_relaxed);
    if (idx >= MAX_EVENTS) {
      full_ = true;
    }
    events_[idx % MAX_EVENTS] = e;
  }

  std::atomic<size_t> write_index_;
  Event *events_;
  bool full_;
};

// RAII 范围事件
class TraceScopeRAII {
 public:
  TraceScopeRAII(const char *name) { Tracer::instance().begin(name); }
  ~TraceScopeRAII() { Tracer::instance().end(); }
};
