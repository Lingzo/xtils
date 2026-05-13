#pragma once

#include <sys/types.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "xtils/config/config.h"
#include "xtils/tasks/event.h"
#include "xtils/tasks/task_group.h"
#include "xtils/tasks/timer.h"

namespace xtils {

class IService;
class App {
 private:
  App();

 public:
  ~App();
  static App* Ins();

  void Register(std::list<std::shared_ptr<IService>> services);
  void Register(std::shared_ptr<IService> p);

  // Deprecated: use Register() instead
  [[deprecated("Use Register() instead")]]
  void registor(std::list<std::shared_ptr<IService>> services) {
    Register(std::move(services));
  }
  [[deprecated("Use Register() instead")]]
  void registor(std::shared_ptr<IService> p) {
    Register(std::move(p));
  }

 public:
  // until shutdown
  void Run();
  // run in backgroud
  void RunDaemon();
  void Init(const std::vector<std::string>& args);
  bool IsRunning();

  // Post a task to run on the main thread (synchronously, in order).
  // WARNING: Long-running tasks will block other Spawn() tasks, SpawnAsync()
  // callbacks, timers, and event handlers. Use SpawnAsync() for I/O or
  // CPU-intensive work.
  void Spawn(Task task);

  // Post a task to run on a worker thread (asynchronously, in parallel).
  // The optional `main` callback runs on the main thread after `task`
  // completes. Use this for I/O operations, network calls, or CPU-intensive
  // work.
  void SpawnAsync(Task task, Task main = nullptr);

  void Every(uint32_t ms, TimerCallback cb);

  void Delay(uint32_t ms, TimerCallback cb);

  // event
  template <typename Event>
  void Emit(const Event& e) {
    em_->Emit<Event>(e);
  }

  template <typename Event, typename TypedCallback>
  void Connect(Event id, TypedCallback cb) {
    em_->Connect<Event>(id, cb);
  }

  template <typename Event, typename TypedCallback>
  void Connect(TypedCallback cb) {
    em_->Connect<Event>(cb);
  }

  const Config& Conf() { return config_; }

  // Deprecated wrappers
  [[deprecated("Use Ins() instead")]]
  static App* ins() { return Ins(); }
  [[deprecated("Use Run() instead")]]
  void run() { Run(); }
  [[deprecated("Use RunDaemon() instead")]]
  void run_daemon() { RunDaemon(); }
  [[deprecated("Use Init() instead")]]
  void init(const std::vector<std::string>& args) { Init(args); }
  [[deprecated("Use IsRunning() instead")]]
  bool is_running() { return IsRunning(); }
  [[deprecated("Use Spawn() instead")]]
  void spawn(Task task) { Spawn(std::move(task)); }
  [[deprecated("Use SpawnAsync() instead")]]
  void spawn_async(Task task, Task main = nullptr) {
    SpawnAsync(std::move(task), std::move(main));
  }
  [[deprecated("Use Every() instead")]]
  void every(uint32_t ms, TimerCallback cb) { Every(ms, std::move(cb)); }
  [[deprecated("Use Delay() instead")]]
  void delay(uint32_t ms, TimerCallback cb) { Delay(ms, std::move(cb)); }
  template <typename Event>
  [[deprecated("Use Emit() instead")]]
  void emit(const Event& e) { Emit<Event>(e); }
  template <typename Event, typename TypedCallback>
  [[deprecated("Use Connect() instead")]]
  void connect(Event id, TypedCallback cb) { Connect<Event>(id, std::move(cb)); }
  template <typename Event, typename TypedCallback>
  [[deprecated("Use Connect() instead")]]
  void connect(TypedCallback cb) { Connect<Event>(std::move(cb)); }
  [[deprecated("Use Conf() instead")]]
  const Config& conf() { return Conf(); }

 private:
  void deinit();

  void parse_args(const std::vector<std::string>& args,
                  bool allow_exit = false);

  void pre_run();

  // init
  void default_config();
  void init_log();
  void init_inspect();
  void print_banner();

 private:
  Config config_;
  std::unique_ptr<EventManager> em_;
  std::shared_ptr<TaskGroup> async_tg_;
  std::unique_ptr<SteadyTimer> timer_;
  std::list<std::shared_ptr<IService>> service_;
  std::atomic<bool> running_{false};
  bool initialized_ = false;
  std::vector<std::string> args_;
  std::thread main_;

  uint32_t major_;
  uint32_t minor_;
  uint32_t patch_;
  std::string build_time_;
};

}  // namespace xtils
