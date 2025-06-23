#pragma once

#include <pthread.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <string>

namespace xtils {
using PlatformThreadId = pid_t;
using ThreadID = pthread_t;
using TimeMillis = std::chrono::milliseconds;
using PlatformHandle = int;
using SocketHandle = int;

inline PlatformThreadId GetThreadId() {
  return static_cast<pid_t>(syscall(__NR_gettid));
}

inline TimeMillis GetWallTimeMs() {
  using clock = std::chrono::steady_clock;
  using std::chrono::duration_cast;
  using std::chrono::milliseconds;
  return duration_cast<milliseconds>(clock::now().time_since_epoch());
};

inline bool MaybeSetThreadName(const std::string& name) {
  char buf[16] = {};
  std::snprintf(buf, sizeof(buf) - 1, "%s", name.c_str());
  return pthread_setname_np(pthread_self(), buf) == 0;
}

}  // namespace xtils
