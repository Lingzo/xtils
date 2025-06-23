#pragma once

#include "xtils/tasks/task_runner.h"
#include "xtils/utils/scoped.h"

namespace xtils {
class EventFd {
 public:
  EventFd();
  ~EventFd();
  EventFd(EventFd&&) noexcept = default;
  EventFd& operator=(EventFd&&) = default;

  // The non-blocking file descriptor that can be polled to wait for the event.
  PlatformHandle fd() const { return event_handle_.get(); }

  // Can be called from any thread.
  void Notify();

  // Can be called from any thread. If more Notify() are queued a Clear() call
  // can clear all of them (up to 16 per call).
  void Clear();

 private:
  // The eventfd, when eventfd is supported, otherwise this is the read end of
  // the pipe for fallback mode.
  ScopedPlatformHandle event_handle_;
};

}  // namespace xtils
