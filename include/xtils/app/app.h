#pragma once

#include <sys/types.h>

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
using Task = std::function<void()>;

class IService;
class App {
 private:
  App();

 public:
  ~App();
  static App* ins();

  void registor(std::list<std::shared_ptr<IService>> services);
  void registor(std::shared_ptr<IService> p);

 public:
  // until shutdown
  void run();
  // run in backgroud
  void run_daemon();
  void init(const std::vector<std::string>& args);
  bool is_running();

  void PostTask(Task task);
  void PostAsyncTask(Task task, Task main = nullptr);

  void every(uint32_t ms, TimerCallback cb);

  void delay(uint32_t ms, TimerCallback cb);

  // event
  template <typename Event>
  void emit(const Event& e) {
    em_->emit<Event>(e);
  }

  template <typename Event, typename TypedCallback>
  void connect(Event id, TypedCallback cb) {
    em_->connect<Event>(id, cb);
  }

  template <typename Event, typename TypedCallback>
  void connect(TypedCallback cb) {
    em_->connect<Event>(cb);
  }

  const Config& conf() { return config_; }

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
  std::shared_ptr<TaskGroup> tg_;
  std::unique_ptr<SteadyTimer> timer_;
  std::list<std::shared_ptr<IService>> service_;
  bool running_ = false;
  bool initialized_ = false;
  std::vector<std::string> args_;
  std::thread main_;

  uint32_t major_;
  uint32_t minor_;
  uint32_t patch_;
  std::string build_time_;
};

}  // namespace xtils
