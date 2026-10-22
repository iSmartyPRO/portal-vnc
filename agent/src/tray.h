#pragma once
#include <atomic>
#include <thread>

namespace pvnc {

// A notification-area ("tray") icon shown in the interactive session while the
// agent runs. Optional, controlled by config.trayIcon. Runs its own message
// loop on a dedicated thread.
class TrayIcon {
 public:
  TrayIcon() = default;
  ~TrayIcon();

  void start();
  void stop();

 private:
  void run();
  std::thread thread_;
  std::atomic<bool> running_{false};
  void* hwnd_ = nullptr;  // HWND
};

}  // namespace pvnc
