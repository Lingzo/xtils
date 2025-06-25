#include "xtils/system/signal_handler.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "xtils/logging/logger.h"

#ifdef __GLIBC__
#include <execinfo.h>
#endif

#ifdef __GNUC__
#include <cxxabi.h>
#endif

#include <unistd.h>

namespace xtils {
namespace system {

std::atomic<bool> SignalHandler::shutdown_requested_{false};
SignalHandler::ShutdownCallback SignalHandler::shutdown_callback_;
bool SignalHandler::initialized_{false};

std::string GetStackTrace() {
  std::stringstream ss;
  ss << "Stack trace:" << std::endl;
#ifdef __GLIBC__
  const int max_frames = 64;
  void* frames[max_frames];

  int frame_count = backtrace(frames, max_frames);
  if (frame_count <= 0) {
    ss << "  Unable to get stack trace" << std::endl;
    return ss.str();
  }

  char** symbols = backtrace_symbols(frames, frame_count);
  if (symbols == nullptr) {
    ss << "  Unable to get symbol names" << std::endl;
    return ss.str();
  }

  for (int i = 0; i < frame_count; ++i) {
    ss << "  [" << i << "] ";

    // Try to demangle C++ function names
    std::string symbol_str(symbols[i]);
    size_t func_name_start = symbol_str.find('(');
    size_t func_name_end = symbol_str.find('+');

    if (func_name_start != std::string::npos &&
        func_name_end != std::string::npos && func_name_end > func_name_start) {
      std::string mangled_name = symbol_str.substr(
          func_name_start + 1, func_name_end - func_name_start - 1);

#ifdef __GNUC__
      int status = 0;
      char* demangled =
          abi::__cxa_demangle(mangled_name.c_str(), nullptr, nullptr, &status);

      if (status == 0 && demangled != nullptr) {
        // Successfully demangled
        ss << symbol_str.substr(0, func_name_start + 1) << demangled
           << symbol_str.substr(func_name_end) << std::endl;
        free(demangled);
      } else {
        // Failed to demangle, use original
        ss << symbols[i] << std::endl;
      }
#else
      // No demangling support, use original
      ss << symbols[i] << std::endl;
#endif
    } else {
      // No function name found in expected format, use original
      ss << symbols[i] << std::endl;
    }
  }

  free(symbols);
#else
  ss << "  Stack trace not available on this platform" << std::endl;
#endif
  return ss.str();
}

void SignalHandler::Initialize(ShutdownCallback callback) {
  if (initialized_) {
    return;
  }

  shutdown_callback_ = callback;
  shutdown_requested_.store(false);

  struct sigaction sa_shutdown;
  struct sigaction sa_crash;

  // Setup shutdown signal handler
  memset(&sa_shutdown, 0, sizeof(sa_shutdown));
  sa_shutdown.sa_sigaction = HandleShutdownSignal;
  sa_shutdown.sa_flags = SA_SIGINFO;
  sigemptyset(&sa_shutdown.sa_mask);

  // Setup crash signal handler
  memset(&sa_crash, 0, sizeof(sa_crash));
  sa_crash.sa_sigaction = HandleCrashSignal;
  sa_crash.sa_flags = SA_SIGINFO;
  sigemptyset(&sa_crash.sa_mask);

  // Install shutdown signal handlers
  sigaction(SIGINT, &sa_shutdown, nullptr);
  sigaction(SIGTERM, &sa_shutdown, nullptr);
#ifdef SIGQUIT
  sigaction(SIGQUIT, &sa_shutdown, nullptr);
#endif

  // Install crash signal handlers
  sigaction(SIGABRT, &sa_crash, nullptr);
  sigaction(SIGSEGV, &sa_crash, nullptr);
#ifdef SIGBUS
  sigaction(SIGBUS, &sa_crash, nullptr);
#endif
#ifdef SIGFPE
  sigaction(SIGFPE, &sa_crash, nullptr);
#endif
#ifdef SIGILL
  sigaction(SIGILL, &sa_crash, nullptr);
#endif

  initialized_ = true;
}

bool SignalHandler::IsShutdownRequested() { return shutdown_requested_.load(); }

void SignalHandler::Cleanup() {
  if (!initialized_) {
    return;
  }

  // Restore default signal handlers
  signal(SIGINT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
#ifdef SIGQUIT
  signal(SIGQUIT, SIG_DFL);
#endif
  signal(SIGABRT, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);
#ifdef SIGBUS
  signal(SIGBUS, SIG_DFL);
#endif
#ifdef SIGFPE
  signal(SIGFPE, SIG_DFL);
#endif
#ifdef SIGILL
  signal(SIGILL, SIG_DFL);
#endif

  shutdown_callback_ = nullptr;
  initialized_ = false;
}

void SignalHandler::HandleShutdownSignal(int sig, siginfo_t* info,
                                         void* context) {
  std::cout << "Received shutdown signal " << GetSignalName(sig) << " (" << sig
            << "), shutting down..." << std::endl;

  if (shutdown_requested_) return;
  shutdown_requested_.store(true);
  signal(sig, SIG_DFL);  // avoid call again

  if (shutdown_callback_) {
    shutdown_callback_();
  }
}

void SignalHandler::Shutdown() {
  LogW("shutdown by user");
  shutdown_requested_.store(true);
  if (shutdown_callback_) {
    shutdown_callback_();
  }
}

void SignalHandler::HandleCrashSignal(int sig, siginfo_t* info, void* context) {
  std::stringstream ss;
  ss << "=== CRASH DETECTED ===" << std::endl;
  ss << "Signal: " << GetSignalName(sig) << " (" << sig << ")" << std::endl;

  if (info) {
    ss << "Signal info:" << std::endl;
    ss << "  si_pid: " << info->si_pid << std::endl;
    ss << "  si_uid: " << info->si_uid << std::endl;
    ss << "  si_code: " << info->si_code << std::endl;

    if (sig == SIGSEGV || sig == SIGBUS) {
      ss << "  fault address: " << info->si_addr << std::endl;
    }
  }

  ss << GetStackTrace() << std::endl;
  ss << "=== END CRASH REPORT ===" << std::endl;
  LogE("\n%s", ss.str().c_str());

  // Restore default handler and re-raise the signal
  signal(sig, SIG_DFL);
  raise(sig);
}

const char* SignalHandler::GetSignalName(int signal) {
  switch (signal) {
    case SIGINT:
      return "SIGINT";
    case SIGTERM:
      return "SIGTERM";
#ifdef SIGQUIT
    case SIGQUIT:
      return "SIGQUIT";
#endif
    case SIGABRT:
      return "SIGABRT";
    case SIGSEGV:
      return "SIGSEGV";
#ifdef SIGBUS
    case SIGBUS:
      return "SIGBUS";
#endif
#ifdef SIGFPE
    case SIGFPE:
      return "SIGFPE";
#endif
#ifdef SIGILL
    case SIGILL:
      return "SIGILL";
#endif
    default:
      return "UNKNOWN";
  }
}

}  // namespace system
}  // namespace xtils
