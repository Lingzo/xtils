#include "event_fd.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "logger.h"
#include "platform.h"

namespace base {

EventFd::~EventFd() = default;

EventFd::EventFd() {
  // Make the pipe non-blocking so that we never block the waking thread (either
  // the main thread or another one) when scheduling a wake-up.
  PlatformHandle fds[2];
  pipe2(fds, O_CLOEXEC | O_NONBLOCK);
  // PlatformHandle  eventfd(0, O_CLOEXEC|O_NONBLOCK);
  event_handle_ = ScopedPlatformHandle(eventfd(0, O_CLOEXEC | O_NONBLOCK));
  // write_fd_ = ScopedFile(fds[1]);
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
}  // namespace base
