#include "tray.h"

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>

#include "resource.h"

#pragma comment(lib, "shell32.lib")

namespace pvnc {

namespace {
constexpr UINT WM_TRAY = WM_APP + 10;
constexpr UINT kIconId = 1;
const wchar_t* kClass = L"PortalVncTrayWnd";

// The PortalVNC server icon, embedded as a resource; falls back to a stock
// computer glyph.
HICON computerIcon() {
  HICON ico = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                            MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                            GetSystemMetrics(SM_CXSMICON),
                                            GetSystemMetrics(SM_CYSMICON), 0));
  if (ico) return ico;
  SHSTOCKICONINFO sii{};
  sii.cbSize = sizeof(sii);
  if (SUCCEEDED(SHGetStockIconInfo(SIID_DESKTOPPC, SHGSI_ICON | SHGSI_SMALLICON, &sii)))
    return sii.hIcon;
  return LoadIcon(nullptr, IDI_APPLICATION);
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_TRAY && (l == WM_RBUTTONUP || l == WM_LBUTTONUP)) {
    POINT p; GetCursorPos(&p);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"PortalVNC — агент активен");
    SetForegroundWindow(h);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
    DestroyMenu(menu);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

TrayIcon::~TrayIcon() { stop(); }

void TrayIcon::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread([this] { run(); });
}

void TrayIcon::stop() {
  if (!running_.exchange(false)) return;
  if (hwnd_) PostMessageW(reinterpret_cast<HWND>(hwnd_), WM_CLOSE, 0, 0);
  if (thread_.joinable()) thread_.join();
}

void TrayIcon::run() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kClass;
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowExW(0, kClass, L"PortalVNC", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                              wc.hInstance, nullptr);
  hwnd_ = hwnd;

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd;
  nid.uID = kIconId;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = WM_TRAY;
  nid.hIcon = computerIcon();
  lstrcpynW(nid.szTip, L"PortalVNC", ARRAYSIZE(nid.szTip));
  Shell_NotifyIconW(NIM_ADD, &nid);

  MSG msg;
  while (running_.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  Shell_NotifyIconW(NIM_DELETE, &nid);
  if (nid.hIcon) DestroyIcon(nid.hIcon);
  DestroyWindow(hwnd);
  hwnd_ = nullptr;
}

}  // namespace pvnc
