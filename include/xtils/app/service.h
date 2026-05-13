#include <string>
#include <vector>

#include "xtils/app/app.h"
#include "xtils/config/config.h"
#include "xtils/utils/weak_ptr.h"

void app_version(uint32_t& major, uint32_t& minor, uint32_t& patch);
// call by internal main function
void app_main(xtils::App& ctx, const std::vector<std::string>& args);

namespace xtils {
class IService {
 public:
  explicit IService(const char* n) : name(n) {}
  virtual void Init() = 0;
  virtual void Deinit() = 0;
  virtual ~IService() = default;

 protected:
  std::string name;
  friend class App;
  xtils::App* ctx;
  Config config;
};

template <typename ServiceType>
class Service : public IService {
 public:
  explicit Service(const char* n) : IService(n), weak_factory_(this) {}
  virtual void Init() = 0;
  virtual void Deinit() = 0;
  auto GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  template <typename T>
  void Emit(const T& e) {
    ctx->Emit<T>(e);
  }

  // Deprecated wrappers
  template <typename T>
  [[deprecated("Use Emit() instead")]]
  void emit(const T& e) { Emit<T>(e); }

 protected:
  WeakPtrFactory<Service<ServiceType>> weak_factory_;
};

/**
 * @brief check if the global context is ok
 * @return true if ok
 */
bool IsOk();
/**
 * @brief init the global context
 * @param args command line arguments
 */
void Init(const std::vector<std::string>& args);

/**
 * @brief init the global context for backward
 * @param argc number of argv
 * @param argv params const char*
 */
void Init(int argc, const char* const argv[]);
/**
 * @brief wait until resource released
 */
void Shutdown();
/**
 * @brief run the main loop, return when shutdown
 */
void RunForever();
/**
 * @brief run as a daemon process
 */
void RunDaemon();

// Deprecated wrappers
[[deprecated("Use IsOk() instead")]]
inline bool isOk() { return IsOk(); }
[[deprecated("Use Init() instead")]]
inline void init(const std::vector<std::string>& args) { Init(args); }
[[deprecated("Use Init() instead")]]
inline void init(int argc, const char* const argv[]) { Init(argc, argv); }
[[deprecated("Use Shutdown() instead")]]
inline void shutdown() { Shutdown(); }
[[deprecated("Use RunForever() instead")]]
inline void run_forever() { RunForever(); }
[[deprecated("Use RunDaemon() instead")]]
inline void run_daemon() { RunDaemon(); }
}  // namespace xtils
