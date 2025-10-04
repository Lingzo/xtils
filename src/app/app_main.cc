#include <vector>

#include "xtils/app/app.h"
#include "xtils/app/service.h"

__attribute__((weak)) int main(int argc, char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  xtils::init(args);
  auto app = xtils::App::ins();
  app_main(*app, args);
  xtils::run_forever();
  return 0;
}
