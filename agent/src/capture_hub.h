#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "capture_gdi.h"

namespace pvnc {

// Grabs the desktop on a background thread and publishes the latest frame with
// a monotonically increasing version, so that any number of client threads can
// read a consistent copy without each driving its own capture.
class CaptureHub {
 public:
  CaptureHub();
  ~CaptureHub();

  void start();
  void stop();

  // Copies the latest frame into `out` and returns its version. Blocks briefly
  // until the first frame is available.
  uint64_t latest(Frame& out);

  // Current dimensions (valid after the first grab).
  int width();
  int height();

 private:
  void loop();

  GdiCapture capture_;
  std::thread thread_;
  std::atomic<bool> running_{false};

  std::mutex mu_;
  std::condition_variable cv_;
  Frame current_;
  uint64_t version_ = 0;
};

}  // namespace pvnc
