#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "xtils/config/config.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {
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
    components_.emplace_back(p);
    return p;
  }
  void registor(std::shared_ptr<Service> p) { components_.emplace_back(p); }
  void remove(std::shared_ptr<Service> p) {
    if (p) {
      for (auto it = components_.begin(); it != components_.end();) {
        if (p.get() == it->get()) {
          it = components_.erase(it);
        } else {
          it++;
        }
      }
    }
  }
  void PostTask(std::function<void()> task);
  void RunBackground(std::function<void()> task,
                     std::function<void()> main = nullptr);
  const Config& conf() { return config_; }

 private:
  void deinit();
  void default_config();
  void init_log();
  void init_inspect();

 private:
  Config config_;
  std::unique_ptr<TaskRunner> task_runner_;
  std::vector<std::unique_ptr<TaskRunner>> pool_;
  std::vector<std::shared_ptr<Service>> components_;
  bool running_ = false;
};

}  // namespace xtils
