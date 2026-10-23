#include "window.h"

#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>

#include <cstdio>
#include <ctime>
#include <string>

#include "files_dialog.h"
#include "resource.h"
#include "sysinfo_dialog.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace pvnc {

namespace {
constexpr UINT WM_FRAME = WM_APP + 1;
constexpr int kToolbarH = 56;
constexpr int kBtnW = 60;

enum {
  ID_INFO = 201, ID_FILES = 202, ID_KEYS = 203, ID_REFRESH = 204,
  ID_SHOT = 205, ID_FULLSCREEN = 206, ID_VIEWONLY = 207, ID_DISCONNECT = 208,
};

// Palette (premium dark).
constexpr COLORREF kBarBg = RGB(0x23, 0x26, 0x2E);
constexpr COLORREF kBarBg2 = RGB(0x1B, 0x1E, 0x25);
constexpr COLORREF kHover = RGB(0x33, 0x38, 0x45);
constexpr COLORREF kAccent = RGB(0x2D, 0x6C, 0xDF);
constexpr COLORREF kIcon = RGB(0xE6, 0xED, 0xF3);
constexpr COLORREF kLabel = RGB(0xA6, 0xAF, 0xBD);
constexpr COLORREF kSep = RGB(0x39, 0x3F, 0x4B);
constexpr COLORREF kGreen = RGB(0x3F, 0xB9, 0x50);

uint32_t specialKeysym(WPARAM vk) {
  switch (vk) {
    case VK_BACK: return 0xFF08; case VK_TAB: return 0xFF09; case VK_RETURN: return 0xFF0D;
    case VK_ESCAPE: return 0xFF1B; case VK_INSERT: return 0xFF63; case VK_DELETE: return 0xFFFF;
    case VK_HOME: return 0xFF50; case VK_END: return 0xFF57; case VK_PRIOR: return 0xFF55;
    case VK_NEXT: return 0xFF56; case VK_LEFT: return 0xFF51; case VK_UP: return 0xFF52;
    case VK_RIGHT: return 0xFF53; case VK_DOWN: return 0xFF54;
    case VK_SHIFT: case VK_LSHIFT: return 0xFFE1; case VK_RSHIFT: return 0xFFE2;
    case VK_CONTROL: case VK_LCONTROL: return 0xFFE3; case VK_RCONTROL: return 0xFFE4;
    case VK_MENU: case VK_LMENU: return 0xFFE9; case VK_RMENU: return 0xFFEA;
    case VK_LWIN: return 0xFFE7; case VK_RWIN: return 0xFFE8;
    default: break;
  }
  if (vk >= VK_F1 && vk <= VK_F12) return 0xFFBE + (uint32_t(vk) - VK_F1);
  return 0;
}

void fillRect(HDC dc, const RECT& r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, &r, b);
  DeleteObject(b);
}
void roundFill(HDC dc, const RECT& r, COLORREF c, int rad) {
  HBRUSH b = CreateSolidBrush(c);
  HBRUSH ob = (HBRUSH)SelectObject(dc, b);
  HPEN op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
  RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
  SelectObject(dc, ob); SelectObject(dc, op);
  DeleteObject(b);
}
}  // namespace

ViewerWindow::ViewerWindow(RfbClient& client, std::wstring host)
    : client_(client), host_(std::move(host)) {}
ViewerWindow::~ViewerWindow() {
  if (iconFont_) DeleteObject(iconFont_);
  if (labelFont_) DeleteObject(labelFont_);
  if (statusFont_) DeleteObject(statusFont_);
}

void ViewerWindow::buildToolbar() {
  items_ = {
      {ID_INFO, L"", L"Computer"},
      {ID_FILES, L"", L"Files"},
      {ID_KEYS, L"", L"Keys"},
      {ID_REFRESH, L"", L"Refresh", false, false, true},
      {ID_SHOT, L"", L"Snapshot"},
      {ID_VIEWONLY, L"", L"View only", true},
      {ID_FULLSCREEN, L"", L"Fullscreen", true},
      {ID_DISCONNECT, L"", L"Disconnect", false, true, false},
  };
}

bool ViewerWindow::create(const wchar_t* title) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = &ViewerWindow::wndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.lpszClassName = L"PortalVncViewerWnd";
  RegisterClassW(&wc);

  iconFont_ = CreateFontW(-22, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, 0, L"Segoe MDL2 Assets");
  labelFont_ = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
  statusFont_ = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Segoe UI Semibold");
  buildToolbar();

  int w = client_.width() > 0 ? client_.width() : 1024;
  int h = client_.height() > 0 ? client_.height() : 768;
  int winW = w > 1600 ? w / 2 : w;
  int winH = (h > 1200 ? h / 2 : h) + kToolbarH;

  hwnd_ = CreateWindowExW(0, L"PortalVncViewerWnd", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                          CW_USEDEFAULT, winW, winH, nullptr, nullptr, wc.hInstance, this);
  if (!hwnd_) return false;
  // Dark immersive title bar to match the toolbar (Win10 2004+/Win11).
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
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

void ViewerWindow::layoutToolbar(HWND h) {
  RECT rc; GetClientRect(h, &rc);
  int x = 8, rx = rc.right - 8;
  for (auto& it : items_) {
    if (it.rightAlign) continue;
    if (it.separatorBefore) x += 10;
    it.rect = {x, 4, x + kBtnW, kToolbarH - 4};
    x += kBtnW + 2;
  }
  for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
    if (!it->rightAlign) continue;
    int w = kBtnW + 20;
    it->rect = {rx - w, 4, rx, kToolbarH - 4};
    rx -= w + 4;
  }
}

LRESULT CALLBACK ViewerWindow::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  return self ? self->handle(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

int ViewerWindow::hitTest(int x, int y) const {
  for (size_t i = 0; i < items_.size(); ++i) {
    const RECT& r = items_[i].rect;
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return (int)i;
  }
  return -1;
}

void ViewerWindow::paintToolbar(HDC dc, const RECT& client) {
  RECT bar{0, 0, client.right, kToolbarH};
  // Vertical gradient background.
  TRIVERTEX v[2] = {
      {0, 0, 0x2300, 0x2600, 0x2E00, 0}, {client.right, kToolbarH, 0x1B00, 0x1E00, 0x2500, 0}};
  GRADIENT_RECT gr{0, 1};
  GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
  RECT bottomLine{0, kToolbarH - 1, client.right, kToolbarH};
  fillRect(dc, bottomLine, kAccent);

  SetBkMode(dc, TRANSPARENT);
  for (size_t i = 0; i < items_.size(); ++i) {
    const auto& it = items_[i];
    bool on = it.toggle && it.on;
    if ((int)i == hover_ || on) {
      RECT hr = it.rect; InflateRect(&hr, -2, -2);
      roundFill(dc, hr, on ? kAccent : kHover, 10);
    }
    if (it.separatorBefore) {
      int sx = it.rect.left - 6;
      RECT sep{sx, 12, sx + 1, kToolbarH - 12};
      fillRect(dc, sep, kSep);
    }
    // Icon
    SelectObject(dc, iconFont_);
    SetTextColor(dc, on ? RGB(0xFF, 0xFF, 0xFF) : kIcon);
    RECT ir = it.rect; ir.bottom = ir.top + 30; ir.top += 6;
    DrawTextW(dc, it.glyph, -1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    // Label
    SelectObject(dc, labelFont_);
    SetTextColor(dc, on ? RGB(0xFF, 0xFF, 0xFF) : kLabel);
    RECT lr = it.rect; lr.top = it.rect.bottom - 18;
    DrawTextW(dc, it.label, -1, &lr, DT_CENTER | DT_SINGLELINE | DT_NOCLIP);
  }

  // Status: connection dot + host, centered-right before the Disconnect button.
  int statusRight = client.right - (kBtnW + 20) - 16;
  SelectObject(dc, statusFont_);
  SetTextColor(dc, RGB(0xC9, 0xD1, 0xD9));
  RECT tr{statusRight - 240, 0, statusRight, kToolbarH};
  std::wstring s = L"Connected  \x2022  " + host_;
  DrawTextW(dc, s.c_str(), -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
  // Green dot before the text.
  SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
  int dotX = tr.right - sz.cx - 14, dotY = kToolbarH / 2 - 4;
  RECT dot{dotX, dotY, dotX + 8, dotY + 8};
  roundFill(dc, dot, kGreen, 8);
}

void ViewerWindow::sendPointer(HWND h, WPARAM wParam, int cx, int cy, int wheel) {
  if (viewOnly_) return;
  RECT rc; GetClientRect(h, &rc);
  int cw = rc.right - rc.left, ch = rc.bottom - rc.top - kToolbarH;
  if (cw <= 0 || ch <= 0 || client_.width() == 0) return;
  int ay = cy - kToolbarH;
  if (ay < 0) return;
  int rx = int((long long)cx * client_.width() / cw);
  int ry = int((long long)ay * client_.height() / ch);
  uint8_t b = 0;
  if (wParam & MK_LBUTTON) b |= 0x01;
  if (wParam & MK_MBUTTON) b |= 0x02;
  if (wParam & MK_RBUTTON) b |= 0x04;
  if (wheel > 0) { client_.pushPointer(b | 0x08, rx, ry); client_.pushPointer(b, rx, ry); }
  else if (wheel < 0) { client_.pushPointer(b | 0x10, rx, ry); client_.pushPointer(b, rx, ry); }
  else client_.pushPointer(b, rx, ry);
}

void ViewerWindow::mapAndSendKey(WPARAM vk, LPARAM lParam, bool down) {
  if (viewOnly_) return;
  uint32_t ks = specialKeysym(vk);
  if (ks == 0) {
    BYTE state[256]; GetKeyboardState(state);
    WORD out = 0; UINT scan = (lParam >> 16) & 0xFF;
    int n = ToAscii((UINT)vk, scan, state, &out, 0);
    if (n == 1) ks = (uint32_t)(out & 0xFF); else return;
  }
  client_.pushKey(ks, down);
}

void ViewerWindow::sendCombo(const unsigned* keys, int n) {
  for (int i = 0; i < n; ++i) client_.pushKey(keys[i], true);
  for (int i = n - 1; i >= 0; --i) client_.pushKey(keys[i], false);
}

void ViewerWindow::showKeysMenu(HWND h) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 1001, L"Ctrl + Alt + Del");
  AppendMenuW(menu, MF_STRING, 1002, L"Alt + Tab");
  AppendMenuW(menu, MF_STRING, 1003, L"Ctrl + Esc  (Start)");
  AppendMenuW(menu, MF_STRING, 1004, L"Alt + F4");
  AppendMenuW(menu, MF_STRING, 1005, L"Windows key");
  AppendMenuW(menu, MF_STRING, 1006, L"Esc");
  // Position below the Keys button.
  RECT br{};
  for (auto& it : items_) if (it.id == ID_KEYS) br = it.rect;
  POINT p{br.left, br.bottom}; ClientToScreen(h, &p);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTBUTTON, p.x, p.y, 0, h, nullptr);
  DestroyMenu(menu);
  switch (cmd) {
    case 1001: { unsigned k[]={0xFFE3,0xFFE9,0xFFFF}; sendCombo(k,3); break; }
    case 1002: { unsigned k[]={0xFFE9,0xFF09}; sendCombo(k,2); break; }
    case 1003: { unsigned k[]={0xFFE3,0xFF1B}; sendCombo(k,2); break; }
    case 1004: { unsigned k[]={0xFFE9,0xFFC1}; sendCombo(k,2); break; }
    case 1005: { unsigned k[]={0xFFE7}; sendCombo(k,1); break; }
    case 1006: { unsigned k[]={0xFF1B}; sendCombo(k,1); break; }
  }
}

void ViewerWindow::saveScreenshot(HWND h) {
  wchar_t path[MAX_PATH]; path[0] = 0;
  wchar_t desktop[MAX_PATH] = L"";
  SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop);
  std::time_t t = std::time(nullptr); std::tm tm{}; localtime_s(&tm, &t);
  swprintf(path, MAX_PATH, L"%s\\PortalVNC-%04d%02d%02d-%02d%02d%02d.bmp", desktop,
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  bool ok = false;
  client_.withFrame([&](const uint8_t* bgra, int fw, int fh) {
    if (!bgra) return;
    BITMAPFILEHEADER fh2{}; BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi); bi.biWidth = fw; bi.biHeight = -fh; bi.biPlanes = 1;
    bi.biBitCount = 32; bi.biCompression = BI_RGB;
    DWORD imgSize = (DWORD)fw * fh * 4;
    fh2.bfType = 0x4D42; fh2.bfOffBits = sizeof(fh2) + sizeof(bi);
    fh2.bfSize = fh2.bfOffBits + imgSize;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(f, &fh2, sizeof(fh2), &wr, nullptr);
    WriteFile(f, &bi, sizeof(bi), &wr, nullptr);
    WriteFile(f, bgra, imgSize, &wr, nullptr);
    CloseHandle(f); ok = true;
  });
  MessageBoxW(h, ok ? (std::wstring(L"Saved to Desktop:\n") + path).c_str()
                    : L"Nothing to capture yet.",
              L"PortalVNC", ok ? MB_ICONINFORMATION : MB_ICONWARNING);
}

void ViewerWindow::toggleFullscreen(HWND h) {
  if (!fullscreen_) {
    savedStyle_ = GetWindowLongW(h, GWL_STYLE);
    savedPlacement_.length = sizeof(savedPlacement_);
    GetWindowPlacement(h, &savedPlacement_);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowLongW(h, GWL_STYLE, savedStyle_ & ~WS_OVERLAPPEDWINDOW);
    SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    fullscreen_ = true;
  } else {
    SetWindowLongW(h, GWL_STYLE, savedStyle_);
    SetWindowPlacement(h, &savedPlacement_);
    SetWindowPos(h, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
    fullscreen_ = false;
  }
  SetFocus(h);
}

void ViewerWindow::onCommand(HWND h, int id) {
  switch (id) {
    case ID_INFO: {
      SetCursor(LoadCursor(nullptr, IDC_WAIT));
      std::string j; bool ok = client_.fetchSystemInfo(j);
      SetCursor(LoadCursor(nullptr, IDC_ARROW));
      if (ok) showSystemInfo(h, j);
      else MessageBoxW(h, L"Could not retrieve system information.", L"PortalVNC", MB_ICONWARNING);
      break;
    }
    case ID_FILES: showFiles(h, client_); break;
    case ID_KEYS: showKeysMenu(h); break;
    case ID_REFRESH: client_.requestRefresh(); break;
    case ID_SHOT: saveScreenshot(h); break;
    case ID_VIEWONLY:
      viewOnly_ = !viewOnly_;
      for (auto& it : items_) if (it.id == ID_VIEWONLY) it.on = viewOnly_;
      InvalidateRect(h, nullptr, FALSE);
      break;
    case ID_FULLSCREEN:
      toggleFullscreen(h);
      for (auto& it : items_) if (it.id == ID_FULLSCREEN) it.on = fullscreen_;
      break;
    case ID_DISCONNECT: DestroyWindow(h); break;
  }
  SetFocus(h);
}

LRESULT ViewerWindow::handle(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE:
      layoutToolbar(h);
      return 0;
    case WM_SIZE:
      layoutToolbar(h);
      InvalidateRect(h, nullptr, FALSE);
      return 0;
    case WM_FRAME: {
      RECT fb{0, kToolbarH, 0, 0}; GetClientRect(h, &fb);
      fb.top = kToolbarH;
      InvalidateRect(h, &fb, FALSE);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
      RECT rc; GetClientRect(h, &rc);
      // Toolbar
      if (ps.rcPaint.top < kToolbarH) {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, kToolbarH);
        HBITMAP ob = (HBITMAP)SelectObject(mem, bmp);
        paintToolbar(mem, rc);
        BitBlt(dc, 0, 0, rc.right, kToolbarH, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
      }
      // Desktop
      client_.withFrame([&](const uint8_t* bgra, int fw, int fh) {
        if (!bgra) return;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = fw; bi.bmiHeader.biHeight = -fh;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, HALFTONE);
        StretchDIBits(dc, 0, kToolbarH, rc.right, rc.bottom - kToolbarH, 0, 0, fw, fh, bgra,
                      &bi, DIB_RGB_COLORS, SRCCOPY);
      });
      EndPaint(h, &ps);
      return 0;
    }
    case WM_MOUSEMOVE: {
      int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
      if (y < kToolbarH) {
        int hv = hitTest(x, y);
        if (hv != hover_) { hover_ = hv; InvalidateRect(h, nullptr, FALSE); }
        if (!trackingLeave_) {
          TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
          TrackMouseEvent(&tme); trackingLeave_ = true;
        }
        SetCursor(LoadCursor(nullptr, hv >= 0 ? IDC_HAND : IDC_ARROW));
        return 0;
      }
      if (hover_ != -1) { hover_ = -1; InvalidateRect(h, nullptr, FALSE); }
      SetFocus(h);
      sendPointer(h, w, x, y, 0);
      return 0;
    }
    case WM_MOUSELEAVE:
      trackingLeave_ = false;
      if (hover_ != -1) { hover_ = -1; InvalidateRect(h, nullptr, FALSE); }
      return 0;
    case WM_LBUTTONDOWN: {
      int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
      if (y < kToolbarH) { int i = hitTest(x, y); if (i >= 0) onCommand(h, items_[i].id); return 0; }
      SetFocus(h); sendPointer(h, w, x, y, 0); return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: {
      int y = GET_Y_LPARAM(l); if (y < kToolbarH) return 0;
      sendPointer(h, w, GET_X_LPARAM(l), y, 0); return 0;
    }
    case WM_MOUSEWHEEL: {
      POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(h, &p);
      if (p.y < kToolbarH) return 0;
      sendPointer(h, LOWORD(w), p.x, p.y, GET_WHEEL_DELTA_WPARAM(w)); return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN: mapAndSendKey(w, l, true); return 0;
    case WM_KEYUP: case WM_SYSKEYUP: mapAndSendKey(w, l, false); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

}  // namespace pvnc
