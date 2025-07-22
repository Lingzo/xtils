#include <memory>

#include "xtils/app/app.h"
#include "xtils/config/config.h"

// must before run_srv
void setup_srv(const std::list<std::shared_ptr<xtils::Service>>& ss);
void setup_srv(const std::shared_ptr<xtils::Service>& ss);
// make sure have main function
void run_srv(int argc, char** argv);

void app_version(uint32_t& major, uint32_t& minor, uint32_t& patch,
                 std::string& build_time);
// call by internal main function
void app_main(int argc, char** argv);

namespace xtils {
class Service {
 public:
  explicit Service(const char* n) : name(n) {}
  virtual void init() = 0;
  virtual void deinit() = 0;

 protected:
  friend class App;
  xtils::App* ctx;
  std::string name;
  Config config;
};
}  // namespace xtils
