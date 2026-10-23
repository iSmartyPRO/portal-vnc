#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "rfb_client.h"

namespace pvnc {

// A modern, dark, custom-drawn viewer window: an icon toolbar on top and the
// remote desktop below. Forwards mouse/keyboard to the RfbClient.
class ViewerWindow {
 public:
  ViewerWindow(RfbClient& client, std::wstring statusHost);
  ~ViewerWindow();

  bool create(const wchar_t* title);
  int runMessageLoop();

 private:
  struct TBItem {
    int id;
    const wchar_t* glyph;   // Segoe MDL2 Assets
    const wchar_t* label;
    bool toggle = false;
    bool rightAlign = false;
    bool separatorBefore = false;
    bool on = false;
    RECT rect{};
  };

  static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(HWND, UINT, WPARAM, LPARAM);

  void buildToolbar();
  void layoutToolbar(HWND);
  void paintToolbar(HDC, const RECT& client);
  int hitTest(int x, int y) const;
  void onCommand(HWND, int id);
  void showKeysMenu(HWND);
  void saveScreenshot(HWND);
  void sendCombo(const unsigned* keysyms, int n);
  void toggleFullscreen(HWND);
  void sendPointer(HWND, WPARAM wParam, int clientX, int clientY, int wheel);
  void mapAndSendKey(WPARAM vk, LPARAM lParam, bool down);

  RfbClient& client_;
  std::wstring host_;
  HWND hwnd_ = nullptr;

  std::vector<TBItem> items_;
  int hover_ = -1;
  bool trackingLeave_ = false;
  bool viewOnly_ = false;
  bool fullscreen_ = false;
  WINDOWPLACEMENT savedPlacement_{};
  LONG savedStyle_ = 0;

  HFONT iconFont_ = nullptr;
  HFONT labelFont_ = nullptr;
  HFONT statusFont_ = nullptr;
};

}  // namespace pvnc
