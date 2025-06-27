#pragma once

#include <stdint.h>

#include <functional>

#include "xtils/system/platform.h"

namespace xtils {
using Task = std::function<void()>;
class TaskRunner {
 public:
  virtual ~TaskRunner() = default;

  virtual void PostTask(std::function<void()>) = 0;

  virtual void PostDelayedTask(std::function<void()>, uint32_t delay_ms) = 0;

  virtual void AddFileDescriptorWatch(PlatformHandle,
                                      std::function<void()>) = 0;

  virtual void RemoveFileDescriptorWatch(PlatformHandle) = 0;

  virtual bool RunsTasksOnCurrentThread() const = 0;
};

}  // namespace xtils
