#include "xtils/app/app.h"

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
};
}  // namespace xtils
