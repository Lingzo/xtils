#pragma once

#include <signal.h>

#include <atomic>
#include <functional>
#include <string>

namespace xtils {
namespace system {
std::string GetStackTrace();

class SignalHandler {
 public:
  using ShutdownCallback = std::function<void()>;

  // Initialize signal handlers with optional shutdown callback
  static void Initialize(ShutdownCallback callback = nullptr);

  // Check if shutdown was requested
  static bool IsShutdownRequested();

  // Cleanup and restore default signal handlers
  static void Cleanup();
  
  static void Shutdown();

 private:
  // Signal handler function for graceful shutdown signals
  static void HandleShutdownSignal(int sig, siginfo_t* info, void* context);

  // Signal handler function for crash signals (with stack trace)
  static void HandleCrashSignal(int sig, siginfo_t* info, void* context);

  // Get signal name as string
  static const char* GetSignalName(int sig);

  static std::atomic<bool> shutdown_requested_;
  static ShutdownCallback shutdown_callback_;
  static bool initialized_;
};

}  // namespace system
}  // namespace xtils
