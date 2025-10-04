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

bool isOk() { return !system::SignalHandler::IsShutdownRequested(); }

void shutdown() { system::SignalHandler::Shutdown(); }

}  // namespace xtils
