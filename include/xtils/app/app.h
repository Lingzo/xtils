#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "xtils/config/config.h"
#include "xtils/tasks/event.h"
#include "xtils/tasks/task_group.h"
#include "xtils/tasks/timer.h"

namespace xtils {
using Task = std::function<void()>;

class Service;
class App {
 public:
  App();
  ~App();

  void init(int argc, char* argv[]);
  void run();
  template <typename T>
  std::shared_ptr<T> registor() {
    auto p = std::make_shared<T>();
    service_.emplace_back(p);
    return p;
  }
  void registor(std::shared_ptr<Service> p) { service_.emplace_back(p); }
  void PostTask(Task task);
  void PostAsyncTask(Task task, Task main = nullptr);

  void every(uint32_t ms, TimerCallback cb);

  void delay(uint32_t ms, TimerCallback cb);

  // event
  void emit(const Event& e);
  void connect(EventId id, OnEvent cb);

  const Config& conf() { return config_; }

 private:
  void deinit();

  // init
  void default_config();
  void init_log();
  void init_inspect();

 private:
  Config config_;
  std::unique_ptr<EventManager> em_;
  std::shared_ptr<TaskGroup> tg_;
  std::unique_ptr<SteadyTimer> timer_;
  std::vector<std::shared_ptr<Service>> service_;
  bool running_ = false;
};

}  // namespace xtils
