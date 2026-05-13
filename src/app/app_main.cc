#include <vector>

#include "xtils/app/app.h"
#include "xtils/app/service.h"

__attribute__((weak)) int main(int argc, const char* const* argv) {
  std::vector<std::string> args(argv, argv + argc);
  xtils::Init(args);
  auto app = xtils::App::Ins();
  app_main(*app, args);
  xtils::RunForever();
  return 0;
}
