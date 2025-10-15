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

/**
 * @brief check if the global context is ok
 * @return true if ok
 */
bool isOk();
/**
 * @brief init the global context
 * @param args command line arguments
 */
void init(const std::vector<std::string>& args);
/**
 * @brief wait until resource released
 */
void shutdown();
/**
 * @brief run the main loop, return when shutdown
 */
void run_forever();
/**
 * @brief run as a daemon process
 */
void run_daemon();
}  // namespace xtils
