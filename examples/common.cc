#include <xtils/app/service.h>
#include <xtils/logging/logger.h>

int main(int argc, char* argv[]) {
  xtils::Init(argc, argv);
  xtils::RunDaemon();
  while (xtils::IsOk()) {
    LogThis();
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  xtils::Shutdown();  // block until resource release
  return 0;
}
