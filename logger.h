#pragma once

#include <assert.h>
#include <stdio.h>

#include <string>

namespace {
constexpr const char* get_filename(const char* path) {
  const char* last_slash = path;
  for (const char* p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') {
      last_slash = p + 1;
    }
  }
  return last_slash;
}
}  // namespace
namespace base {
std::string GetStackTrace();
}

#define _LOG(fmt, ...)                                                     \
  printf("%s:%d %s " fmt "\n", get_filename(__FILE__), __LINE__, __func__, \
         ##__VA_ARGS__)
#define LogD(x, ...) _LOG(x, ##__VA_ARGS__)
#define LogI(x, ...) _LOG(x, ##__VA_ARGS__)
#define LogW(x, ...) _LOG(x, ##__VA_ARGS__)
#define LogThis() LogI("==This==")

#define CHECK(expr, ...)                                    \
  do {                                                      \
    if (!(expr)) {                                          \
      LogW("Assert -- " #expr " -- " #__VA_ARGS__);         \
      fprintf(stderr, "%s", base::GetStackTrace().c_str()); \
    }                                                       \
  } while (0)
#define DCHECK(expr) CHECK(expr)
#define FATAL(x, ...)       \
  do {                      \
    LogW(x, ##__VA_ARGS__); \
    abort();                \
  } while (0);
