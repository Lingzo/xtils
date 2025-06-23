#include "xtils/system/event_fd.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "xtils/logging/logger.h"

namespace xtils {

EventFd::~EventFd() = default;

EventFd::EventFd() {
  event_handle_ = ScopedPlatformHandle(eventfd(0, O_CLOEXEC | O_NONBLOCK));
}

void EventFd::Notify() {
  const eventfd_t value = 1;
  ssize_t ret = eventfd_write(event_handle_.get(), value);
  if (ret < 0 && errno != EAGAIN) LogI("EventFd::Notify()");
}

void EventFd::Clear() {
  eventfd_t value = 0;
  ssize_t ret = eventfd_read(event_handle_.get(), &value);
  if (ret < 0 && errno != EAGAIN) LogI("EventFd::Clear()");
}
}  // namespace xtils
