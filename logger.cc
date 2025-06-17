
#include "logger.h"

#include <cxxabi.h>    // For name demangling.
#include <execinfo.h>  // To get stack trace in error messages.
#include <stdlib.h>

#include <ctime>
#include <string>

namespace base {

static bool LocateSymbolRange(const std::string &trace_name, std::size_t *begin,
                              std::size_t *end) {
  // Find the first '_' with leading ' ' or '('.
  *begin = std::string::npos;
  for (std::size_t i = 1; i < trace_name.size(); ++i) {
    if (trace_name[i] != '_') {
      continue;
    }
    if (trace_name[i - 1] == ' ' || trace_name[i - 1] == '(') {
      *begin = i;
      break;
    }
  }
  if (*begin == std::string::npos) {
    return false;
  }
  *end = trace_name.find_first_of(" +", *begin);
  return *end != std::string::npos;
}

static std::string Demangle(const std::string &trace_name) {
  std::size_t begin, end;
  if (!LocateSymbolRange(trace_name, &begin, &end)) {
    return trace_name;
  }
  std::string symbol = trace_name.substr(begin, end - begin);
  int status;
  char *demangled_name = abi::__cxa_demangle(symbol.c_str(), 0, 0, &status);
  if (status == 0 && demangled_name != nullptr) {
    symbol = demangled_name;
    free(demangled_name);
  }
  return trace_name.substr(0, begin) + symbol +
         trace_name.substr(end, std::string::npos);
}

std::string GetStackTrace() {
  std::string ans;
  constexpr const std::size_t kMaxTraceSize = 30;

  void *trace[kMaxTraceSize];
  // Get the trace.
  std::size_t size = backtrace(trace, kMaxTraceSize);
  // Get the trace symbols.
  char **trace_symbol = backtrace_symbols(trace, size);
  if (trace_symbol == nullptr) return ans;

  // Compose a human-readable backtrace string.
  ans += "[ Stack-Trace: ]\n";
  for (std::size_t i = 0; i < size; ++i) {
    ans += Demangle(trace_symbol[i]) + "\n";
  }

  // We must free the array of pointers allocated by backtrace_symbols(),
  // but not the strings themselves.
  free(trace_symbol);
  return ans;
}
}  // namespace base
