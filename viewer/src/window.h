#pragma once
#include <windows.h>

#include "rfb_client.h"

namespace pvnc {

// A simple Win32 window that displays the remote framebuffer and forwards
// mouse/keyboard input to the RfbClient. Runs its message loop on the calling
// (main) thread.
class ViewerWindow {
 public:
  explicit ViewerWindow(RfbClient& client);
  ~ViewerWindow();

  bool create(const wchar_t* title);
  int runMessageLoop();

 private:
  static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(HWND, UINT, WPARAM, LPARAM);
  void createToolbar(HWND);
  void layout(HWND);
  void onCommand(HWND, int id);
  void sendCtrlAltDel();
  void toggleFullscreen(HWND);
  void sendPointer(HWND, WPARAM wParam, int clientX, int clientY, int wheel);
  void mapAndSendKey(WPARAM vk, LPARAM lParam, bool down);

  RfbClient& client_;
  HWND hwnd_ = nullptr;
  bool fullscreen_ = false;
  WINDOWPLACEMENT savedPlacement_{};
  LONG savedStyle_ = 0;
};

}  // namespace pvnc
