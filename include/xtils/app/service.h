#include "xtils/app/app.h"

namespace xtils {
class Service {
 public:
  explicit Service() = default;
  virtual void init() = 0;
  virtual void deinit() = 0;

 protected:
  friend class App;
  xtils::App* ctx;
};
}  // namespace xtils
