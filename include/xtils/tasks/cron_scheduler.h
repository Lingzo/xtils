#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

namespace xtils {
class CronScheduler {
 public:
  using TaskID = uint64_t;
  using Clock = std::chrono::system_clock;
  using TimePoint = Clock::time_point;
  using Seconds = std::chrono::seconds;
  using Minutes = std::chrono::minutes;
  using Hours = std::chrono::hours;

  struct TaskInfo {
    TaskID id;
    std::string type;
    bool active;
    std::string schedule;
    std::time_t lastRun;
  };

  explicit CronScheduler(int tzOffsetMinutes = 0, bool testMode = false)
      : tzOffsetMinutes_(tzOffsetMinutes),
        running_(false),
        nextId_(1),
        testMode_(testMode) {}

  ~CronScheduler() { stop(); }

  TaskID every(Seconds interval, std::function<void()> fn) {
    return addTask(TaskType::Interval, interval, {}, {}, {}, {}, {}, {},
                   std::move(fn));
  }

  TaskID cron(std::set<int> seconds, std::set<int> minutes, std::set<int> hours,
              std::set<int> days, std::set<int> months, std::set<int> weekdays,
              std::function<void()> fn) {
    return addTask(TaskType::Cron, {}, seconds, minutes, hours, days, months,
                   weekdays, std::move(fn));
  }

  void start() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (running_) return;
    running_ = true;
    if (!testMode_) worker_ = std::thread([this] { schedulerThread(); });
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lk(mutex_);
      running_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  bool cancel(TaskID id) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (auto it = tasks_.find(id); it != tasks_.end()) {
      it->second.active = false;
      rebuildQueue();
      cv_.notify_all();
      return true;
    }
    return false;
  }

  std::optional<TaskInfo> getTaskInfo(TaskID id) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (auto it = tasks_.find(id); it != tasks_.end()) {
      const auto& t = it->second;
      TaskInfo info;
      info.id = t.id;
      info.active = t.active;
      info.type = (t.type == TaskType::Interval ? "Interval" : "Cron");
      info.lastRun = t.lastRun.time_since_epoch().count()
                         ? Clock::to_time_t(t.lastRun)
                         : 0;
      info.schedule = describeTask(t);
      return info;
    }
    return std::nullopt;
  }
  // for testing
  void triggerCheck(TimePoint now) {
    if (!testMode_) return;
    runOnce(now);
  }

  static std::tm advanceSec(std::tm tm, int sec) {
    std::time_t tt = timegm(&tm) + sec;
    std::tm newtm{};
    gmtime_r(&tt, &newtm);
    return newtm;
  }

  static std::tm toLocalTm(TimePoint tp, int tzOffsetMinutes) {
    tp += std::chrono::minutes(tzOffsetMinutes);
    std::time_t tt = Clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    return tm;
  }

  static TimePoint fromLocalTm(std::tm tm, int tzOffsetMinutes) {
    std::time_t tt = timegm(&tm) - tzOffsetMinutes * 60;
    return Clock::from_time_t(tt);
  }

 private:
  enum class TaskType { Interval, Cron };

  struct Task {
    TaskID id;
    TaskType type;
    std::chrono::seconds interval;
    std::set<int> seconds, minutes, hours, days, months, weekdays;
    std::function<void()> fn;
    TimePoint lastRun{};
    TimePoint nextRun{};
    bool active = true;
  };

  std::unordered_map<TaskID, Task> tasks_;
  std::atomic<bool> running_;
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  int tzOffsetMinutes_;
  std::atomic<TaskID> nextId_;
  bool testMode_;

  struct TaskCompare {
    bool operator()(Task* a, Task* b) const { return a->nextRun > b->nextRun; }
  };
  std::priority_queue<Task*, std::vector<Task*>, TaskCompare> taskQueue_;

  TaskID addTask(TaskType type, std::chrono::seconds interval,
                 std::set<int> seconds, std::set<int> minutes,
                 std::set<int> hours, std::set<int> days, std::set<int> months,
                 std::set<int> weekdays, std::function<void()> fn) {
    TaskID id = nextId_++;
    Task t{id,    type, interval, seconds,  minutes,
           hours, days, months,   weekdays, fn};
    t.nextRun = calcNextRunTime(t, Clock::now());
    std::lock_guard<std::mutex> lk(mutex_);
    tasks_[id] = t;
    rebuildQueue();
    cv_.notify_all();
    return id;
  }

  void rebuildQueue() {
    std::priority_queue<Task*, std::vector<Task*>, TaskCompare> q;
    for (auto& [_, t] : tasks_)
      if (t.active) q.push(&t);
    taskQueue_ = std::move(q);
  }

  void schedulerThread() {
    std::unique_lock<std::mutex> lk(mutex_);
    while (running_) {
      cv_.wait(lk, [&] { return !taskQueue_.empty() || !running_; });
      if (!running_) break;

      Task* t = taskQueue_.top();
      if (!t->active) {
        taskQueue_.pop();
        continue;
      }

      auto now = Clock::now();
      if (t->nextRun <= now) {
        taskQueue_.pop();
        TimePoint prevNext = t->nextRun;
        lk.unlock();
        try {
          t->fn();
        } catch (...) {
        }
        lk.lock();
        t->lastRun = prevNext;
        t->nextRun = calcNextRunTime(*t, now);
        taskQueue_.push(t);
      } else {
        auto dur = t->nextRun - Clock::now();
        cv_.wait_for(lk, dur, [&] { return !running_; });
      }
    }
  }

  TimePoint calcNextRunTime(const Task& t, TimePoint from) {
    if (t.type == TaskType::Interval) return from + t.interval;

    std::tm tm = toLocalTm(from + Seconds(1), tzOffsetMinutes_);

    // 最多跳一年（防止死循环）
    for (int i = 0; i < 400; ++i) {
      // 检查各字段是否匹配
      if ((!t.seconds.empty() && !t.seconds.count(tm.tm_sec)) ||
          (!t.minutes.empty() && !t.minutes.count(tm.tm_min)) ||
          (!t.hours.empty() && !t.hours.count(tm.tm_hour)) ||
          (!t.days.empty() && !t.days.count(tm.tm_mday)) ||
          (!t.months.empty() && !t.months.count(tm.tm_mon + 1)) ||
          (!t.weekdays.empty() && !t.weekdays.count(tm.tm_wday))) {
        // 按字段跳转（优先级：秒→分→时→日→月→年）
        if (!t.seconds.empty() && !t.seconds.count(tm.tm_sec)) {
          auto it = t.seconds.lower_bound(tm.tm_sec + 1);
          if (it == t.seconds.end()) {
            tm.tm_min++;
            tm.tm_sec = *t.seconds.begin();
          } else {
            tm.tm_sec = *it;
          }
          mktime(&tm);
          continue;
        }

        if (!t.minutes.empty() && !t.minutes.count(tm.tm_min)) {
          auto it = t.minutes.lower_bound(tm.tm_min + 1);
          if (it == t.minutes.end()) {
            tm.tm_hour++;
            tm.tm_min = *t.minutes.begin();
          } else {
            tm.tm_min = *it;
          }
          tm.tm_sec = t.seconds.empty() ? 0 : *t.seconds.begin();
          mktime(&tm);
          continue;
        }

        if (!t.hours.empty() && !t.hours.count(tm.tm_hour)) {
          auto it = t.hours.lower_bound(tm.tm_hour + 1);
          if (it == t.hours.end()) {
            tm.tm_mday++;
            tm.tm_hour = *t.hours.begin();
          } else {
            tm.tm_hour = *it;
          }
          tm.tm_min = t.minutes.empty() ? 0 : *t.minutes.begin();
          tm.tm_sec = t.seconds.empty() ? 0 : *t.seconds.begin();
          mktime(&tm);
          continue;
        }

        if (!t.days.empty() && !t.days.count(tm.tm_mday)) {
          tm.tm_mday++;
          tm.tm_hour = t.hours.empty() ? 0 : *t.hours.begin();
          tm.tm_min = t.minutes.empty() ? 0 : *t.minutes.begin();
          tm.tm_sec = t.seconds.empty() ? 0 : *t.seconds.begin();
          mktime(&tm);
          continue;
        }

        if (!t.months.empty() && !t.months.count(tm.tm_mon + 1)) {
          auto it = t.months.lower_bound(tm.tm_mon + 2);
          if (it == t.months.end()) {
            tm.tm_year++;
            tm.tm_mon = *t.months.begin() - 1;
          } else {
            tm.tm_mon = *it - 1;
          }
          tm.tm_mday = t.days.empty() ? 1 : *t.days.begin();
          tm.tm_hour = t.hours.empty() ? 0 : *t.hours.begin();
          tm.tm_min = t.minutes.empty() ? 0 : *t.minutes.begin();
          tm.tm_sec = t.seconds.empty() ? 0 : *t.seconds.begin();
          mktime(&tm);
          continue;
        }

        // 星期跳转
        if (!t.weekdays.empty() && !t.weekdays.count(tm.tm_wday)) {
          tm.tm_mday++;
          tm.tm_hour = t.hours.empty() ? 0 : *t.hours.begin();
          tm.tm_min = t.minutes.empty() ? 0 : *t.minutes.begin();
          tm.tm_sec = t.seconds.empty() ? 0 : *t.seconds.begin();
          mktime(&tm);
          continue;
        }
      }

      return fromLocalTm(tm, tzOffsetMinutes_);
    }

    // fallback（超过一年）
    return from + Hours(24 * 365);
  }

  std::string describeTask(const Task& t) {
    if (t.type == TaskType::Interval)
      return "every " + std::to_string(t.interval.count()) + "s";
    auto setToStr = [](const std::set<int>& s) {
      if (s.empty()) return std::string("*");
      std::string r;
      for (int v : s) r += std::to_string(v) + ",";
      if (!r.empty()) r.pop_back();
      return r;
    };
    return "cron " + setToStr(t.seconds) + " " + setToStr(t.minutes) + " " +
           setToStr(t.hours) + " " + setToStr(t.days) + " " +
           setToStr(t.months) + " " + setToStr(t.weekdays);
  }

  void runOnce(TimePoint now) {
    std::vector<std::function<void()>> toRun;
    for (auto& [_, t] : tasks_) {
      if (!t.active) continue;
      if (t.nextRun <= now) {
        t.lastRun = t.nextRun;
        t.nextRun = calcNextRunTime(t, now);
        toRun.push_back(t.fn);
      }
    }
    for (auto& fn : toRun) {
      try {
        fn();
      } catch (...) {
      }
    }
  }
};

}  // namespace xtils
