#include "xtils/app/service.h"

#include "xtils/app/app.h"

__attribute__((weak)) void app_version(uint32_t& major, uint32_t& minor,
                                       uint32_t& patch,
                                       std::string& build_time) {
  major = 1;
  minor = 1;
  patch = 0;
  build_time = __DATE__ " " __TIME__;
}

__attribute__((weak)) void app_main(int argc, char** argv) {}

void setup_srv(const std::list<std::shared_ptr<xtils::Service>>& ss) {
  xtils::App::ins()->registor(ss);
}
void setup_srv(const std::shared_ptr<xtils::Service>& s) {
  xtils::App::ins()->registor(s);
}

void run_srv(int argc, char** argv) { xtils::App::ins()->run(argc, argv); }
