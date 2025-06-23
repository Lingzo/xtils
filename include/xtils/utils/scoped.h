#pragma once

#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#include <functional>
#include <string>

#include "xtils/logging/logger.h"
#include "xtils/system/platform.h"

namespace xtils {

namespace internal {
// Used for the most common cases of ScopedResource where there is only one
// invalid value.
template <typename T, T InvalidValue>
struct DefaultValidityChecker {
  static bool IsValid(T t) { return t != InvalidValue; }
};
}  // namespace internal

// RAII classes for auto-releasing fds and dirs.
// if T is a pointer type, InvalidValue must be nullptr. Doing otherwise
// causes weird unexpected behaviors (See https://godbolt.org/z/5nGMW4).
template <typename T, int (*CloseFunction)(T), T InvalidValue,
          bool CheckClose = true,
          class Checker = internal::DefaultValidityChecker<T, InvalidValue>>
class ScopedResource {
 public:
  using ValidityChecker = Checker;
  static constexpr T kInvalid = InvalidValue;

  explicit ScopedResource(T t = InvalidValue) : t_(t) {}
  ScopedResource(ScopedResource&& other) noexcept {
    t_ = other.t_;
    other.t_ = InvalidValue;
  }
  ScopedResource& operator=(ScopedResource&& other) {
    t_ = other.t_;
    other.t_ = InvalidValue;
    return *this;
  }
  T get() const { return t_; }
  T operator*() const { return t_; }
  explicit operator bool() const { return Checker::IsValid(t_); }
  void reset(T r = InvalidValue) {
    if (Checker::IsValid(t_)) {
      int res = CloseFunction(t_);
      if (CheckClose) CHECK(res == 0);
    }
    t_ = r;
  }
  T release() {
    T t = t_;
    t_ = InvalidValue;
    return t;
  }
  ~ScopedResource() { reset(InvalidValue); }

 private:
  ScopedResource(const ScopedResource&) = delete;
  ScopedResource& operator=(const ScopedResource&) = delete;
  T t_;
};

inline int CloseFile(int fd) { return ::close(fd); }

// Use this for file resources obtained via open() and similar APIs.
using ScopedFile = ScopedResource<int, CloseFile, -1>;
using ScopedFstream = ScopedResource<FILE*, fclose, nullptr>;

using ScopedPlatformHandle = ScopedFile;

using ScopedDir = ScopedResource<DIR*, closedir, nullptr>;

class Scoped {
 public:
  explicit Scoped(std::function<void()> defer) : defer_(defer) {}
  ~Scoped() {
    if (defer_) defer_();
  }

 private:
  std::function<void()> defer_;
};

}  // namespace xtils
