#include <vector>

#include "xtils/app/app.h"
#include "xtils/config/config.h"

void app_version(uint32_t& major, uint32_t& minor, uint32_t& patch);
// call by internal main function
void app_main(xtils::App& ctx, const std::vector<std::string>& args);

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

bool isOk();
void init(const std::vector<std::string>& args);
void shutdown();
void run_forever();
}  // namespace xtils
