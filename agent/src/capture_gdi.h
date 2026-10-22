#pragma once
#include <windows.h>

#include <cstdint>
#include <vector>

namespace pvnc {

// A captured frame: 32-bit BGRA, top-down, stride == width*4.
struct Frame {
  int width = 0;
  int height = 0;
  int stride = 0;
  std::vector<uint8_t> bgra;
};

// Captures the primary desktop via GDI BitBlt into a top-down DIB. This path
// works for the ordinary interactive desktop; the secure-desktop/lock-screen
// path (session-0 helper) is a later plan. GDI is used rather than DXGI
// precisely because DXGI Desktop Duplication is unavailable on the secure
// desktop where the agent must eventually run.
class GdiCapture {
 public:
  GdiCapture();
  ~GdiCapture();
  GdiCapture(const GdiCapture&) = delete;
  GdiCapture& operator=(const GdiCapture&) = delete;

  // Grabs the current screen into `out`. Returns false on failure.
  bool grab(Frame& out);

  int width() const { return width_; }
  int height() const { return height_; }

 private:
  void ensureSized();
  int width_ = 0;
  int height_ = 0;
  HDC screenDc_ = nullptr;
  HDC memDc_ = nullptr;
  HBITMAP dib_ = nullptr;
  void* bits_ = nullptr;
};

}  // namespace pvnc
