#pragma once
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "capture_gdi.h"
#include "input.h"

namespace pvnc {

// One input event queued from a client thread, applied on the capture thread
// (which is the thread attached to the current input desktop).
struct InputEvent {
  bool isKey = false;
  // pointer
  uint8_t buttons = 0;
  int x = 0, y = 0;
  // key
  uint32_t keysym = 0;
  bool down = false;
};

// Grabs the desktop on a background thread and publishes the latest frame with
// a monotonically increasing version. The same thread follows the active input
// desktop (Default / Winlogon / screen-saver) so that — when the process runs
// as SYSTEM in the user's session — capture and input work on the lock screen
// and during UAC. Client threads feed input through enqueue*(); it is applied
// on this one desktop-attached thread.
class CaptureHub {
 public:
  CaptureHub();
  ~CaptureHub();

  void start();
  void stop();

  // Copies the latest frame into `out`; returns its version. Blocks briefly
  // until the first frame is available.
  uint64_t latest(Frame& out);

  int width();
  int height();

  void enqueuePointer(uint8_t buttons, int x, int y);
  void enqueueKey(uint32_t keysym, bool down);

 private:
  void loop();
  bool followInputDesktop();  // returns true if the desktop changed

  std::thread thread_;
  std::atomic<bool> running_{false};

  std::mutex mu_;
  std::condition_variable cv_;
  Frame current_;
  uint64_t version_ = 0;

  std::mutex inMu_;
  std::vector<InputEvent> inQueue_;

  HDESK desk_ = nullptr;
  std::wstring deskName_;
  std::unique_ptr<GdiCapture> cap_;
  std::unique_ptr<InputInjector> input_;
};

}  // namespace pvnc
