#include "window.h"

#include <windowsx.h>

namespace pvnc {

namespace {
constexpr UINT WM_FRAME = WM_APP + 1;
const wchar_t* kClass = L"PortalVncViewerWnd";

// Maps a Windows virtual-key to an X11 keysym for the non-character keys.
uint32_t specialKeysym(WPARAM vk) {
  switch (vk) {
    case VK_BACK: return 0xFF08;
    case VK_TAB: return 0xFF09;
    case VK_RETURN: return 0xFF0D;
    case VK_ESCAPE: return 0xFF1B;
    case VK_INSERT: return 0xFF63;
    case VK_DELETE: return 0xFFFF;
    case VK_HOME: return 0xFF50;
    case VK_END: return 0xFF57;
    case VK_PRIOR: return 0xFF55;
    case VK_NEXT: return 0xFF56;
    case VK_LEFT: return 0xFF51;
    case VK_UP: return 0xFF52;
    case VK_RIGHT: return 0xFF53;
    case VK_DOWN: return 0xFF54;
    case VK_SHIFT: case VK_LSHIFT: return 0xFFE1;
    case VK_RSHIFT: return 0xFFE2;
    case VK_CONTROL: case VK_LCONTROL: return 0xFFE3;
    case VK_RCONTROL: return 0xFFE4;
    case VK_MENU: case VK_LMENU: return 0xFFE9;
    case VK_RMENU: return 0xFFEA;
    case VK_LWIN: return 0xFFE7;
    case VK_RWIN: return 0xFFE8;
    default: break;
  }
  if (vk >= VK_F1 && vk <= VK_F12) return 0xFFBE + (uint32_t(vk) - VK_F1);
  return 0;
}
}  // namespace

ViewerWindow::ViewerWindow(RfbClient& client) : client_(client) {}
ViewerWindow::~ViewerWindow() {}

bool ViewerWindow::create(const wchar_t* title) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = &ViewerWindow::wndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = kClass;
  RegisterClassW(&wc);

  int w = client_.width() > 0 ? client_.width() : 1024;
  int h = client_.height() > 0 ? client_.height() : 768;
  // Open at a comfortable fraction of the remote size.
  int winW = w > 1600 ? w / 2 : w;
  int winH = h > 1200 ? h / 2 : h;

  hwnd_ = CreateWindowExW(0, kClass, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                          CW_USEDEFAULT, winW, winH, nullptr, nullptr,
                          wc.hInstance, this);
  if (!hwnd_) return false;

  client_.onFrameUpdated([this] { PostMessageW(hwnd_, WM_FRAME, 0, 0); });
  ShowWindow(hwnd_, SW_SHOW);
  return true;
}

int ViewerWindow::runMessageLoop() {
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}

LRESULT CALLBACK ViewerWindow::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (self) return self->handle(h, m, w, l);
  return DefWindowProcW(h, m, w, l);
}

void ViewerWindow::sendPointer(HWND h, WPARAM wParam, int cx, int cy, int wheel) {
  RECT rc; GetClientRect(h, &rc);
  int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
  if (cw <= 0 || ch <= 0 || client_.width() == 0) return;
  int rx = int((long long)cx * client_.width() / cw);
  int ry = int((long long)cy * client_.height() / ch);

  uint8_t b = 0;
  if (wParam & MK_LBUTTON) b |= 0x01;
  if (wParam & MK_MBUTTON) b |= 0x02;
  if (wParam & MK_RBUTTON) b |= 0x04;
  buttons_ = b;

  if (wheel > 0) { client_.pushPointer(b | 0x08, rx, ry); client_.pushPointer(b, rx, ry); }
  else if (wheel < 0) { client_.pushPointer(b | 0x10, rx, ry); client_.pushPointer(b, rx, ry); }
  else { client_.pushPointer(b, rx, ry); }
}

void ViewerWindow::mapAndSendKey(WPARAM vk, LPARAM lParam, bool down) {
  uint32_t ks = specialKeysym(vk);
  if (ks == 0) {
    // Character-producing key: translate with the current keyboard state.
    BYTE state[256];
    GetKeyboardState(state);
    WORD out = 0;
    UINT scan = (lParam >> 16) & 0xFF;
    int n = ToAscii((UINT)vk, scan, state, &out, 0);
    if (n == 1) ks = (uint32_t)(out & 0xFF);
    else return;  // dead key / no char
  }
  client_.pushKey(ks, down);
}

LRESULT ViewerWindow::handle(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_FRAME:
      InvalidateRect(h, nullptr, FALSE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      client_.withFrame([&](const uint8_t* bgra, int fw, int fh) {
        if (!bgra) return;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = fw;
        bi.bmiHeader.biHeight = -fh;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        RECT rc; GetClientRect(h, &rc);
        SetStretchBltMode(dc, HALFTONE);
        StretchDIBits(dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                      0, 0, fw, fh, bgra, &bi, DIB_RGB_COLORS, SRCCOPY);
      });
      EndPaint(h, &ps);
      return 0;
    }
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
      SetFocus(h);
      sendPointer(h, w, GET_X_LPARAM(l), GET_Y_LPARAM(l), 0);
      return 0;
    case WM_MOUSEWHEEL: {
      POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ScreenToClient(h, &p);
      int delta = GET_WHEEL_DELTA_WPARAM(w);
      sendPointer(h, LOWORD(w), p.x, p.y, delta);
      return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
      mapAndSendKey(w, l, true);
      return 0;
    case WM_KEYUP: case WM_SYSKEYUP:
      mapAndSendKey(w, l, false);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

}  // namespace pvnc
