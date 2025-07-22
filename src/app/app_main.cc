#include "xtils/app/app.h"
#include "xtils/app/service.h"

__attribute__((weak)) int main(int argc, char** argv) {
  app_main(argc, argv);
  auto app = xtils::App::ins();
  app->run(argc, argv);
  return 0;
}
