#include "xtils/app/service.h"

#include <vector>

#include "xtils/app/app.h"
#include "xtils/system/signal_handler.h"

__attribute__((weak)) void app_version(uint32_t& major, uint32_t& minor,
                                       uint32_t& patch) {
  major = 1;
  minor = 1;
  patch = 0;
}

__attribute__((weak)) void app_main(xtils::App& ctx,
                                    const std::vector<std::string>& argv) {}

namespace xtils {
void init(const std::vector<std::string>& args) { App::ins()->init(args); }

void run_forever() { App::ins()->run(); }
void run_daemon() { App::ins()->run_daemon(); }

bool isOk() { return !system::SignalHandler::IsShutdownRequested(); }

void shutdown() {
  if (isOk()) system::SignalHandler::Shutdown();
  while (App::ins()->is_running()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

}  // namespace xtils
