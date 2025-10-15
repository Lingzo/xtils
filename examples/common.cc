#include <xtils/app/service.h>
#include <xtils/logging/logger.h>

int main(int argc, char* argv[]) {
  xtils::init({argv, argv + argc});
  xtils::run_daemon();
  while (xtils::isOk()) {
    LogThis();
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  xtils::shutdown();  // block until resource release
  return 0;
}
