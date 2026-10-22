#pragma once
#include <cstdint>

namespace pvnc {

// Injects pointer and keyboard events received over RFB into the active
// desktop via SendInput. Coordinates are in screen pixels.
class InputInjector {
 public:
  InputInjector(int screenWidth, int screenHeight);

  // RFB PointerEvent: buttonMask bit0=left,bit1=middle,bit2=right,
  // bit3=wheel-up,bit4=wheel-down.
  void pointer(uint8_t buttonMask, int x, int y);

  // RFB KeyEvent: X11 keysym, down=true on press.
  void key(uint32_t keysym, bool down);

  int width() const { return w_; }
  int height() const { return h_; }

 private:
  int w_, h_;
  uint8_t lastButtons_ = 0;
};

}  // namespace pvnc
