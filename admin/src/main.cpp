#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

#include "resource.h"
#include "ui_agent.h"
#include "ui_ca.h"
#include "ui_common.h"

#ifndef PVNC_VERSION
#define PVNC_VERSION "0.0.0"
#endif

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace pvnc {
namespace {

constexpr int kTabH = 28;
constexpr int kClientW = 760;
constexpr int kClientH = 700;
constexpr int IDM_ABOUT = 40001;

// Tab order: Agent first (the common, day-to-day task), CA last (one-time setup).
constexpr int kTabAgent = 0;
constexpr int kTabCa = 1;

struct App {
  HWND tabs = nullptr;
  CaPanel ca;
  AgentPanel agent;
};

void layout(HWND h, App* app) {
  RECT rc;
  GetClientRect(h, &rc);
  int w = rc.right, ht = rc.bottom;
  MoveWindow(app->tabs, 0, 0, w, kTabH, TRUE);
  MoveWindow(app->agent.hwnd(), 0, kTabH, w, ht - kTabH, TRUE);
  MoveWindow(app->ca.hwnd(), 0, kTabH, w, ht - kTabH, TRUE);
}

void selectTab(App* app, int idx) {
  ShowWindow(app->agent.hwnd(), idx == kTabAgent ? SW_SHOW : SW_HIDE);
  ShowWindow(app->ca.hwnd(), idx == kTabCa ? SW_SHOW : SW_HIDE);
  if (idx == kTabAgent) app->agent.onShow();
}

HRESULT CALLBACK aboutCallback(HWND, UINT msg, WPARAM, LPARAM lParam, LONG_PTR) {
  if (msg == TDN_HYPERLINK_CLICKED)
    ShellExecuteW(nullptr, L"open", (LPCWSTR)lParam, nullptr, nullptr, SW_SHOWNORMAL);
  return S_OK;
}

void showAbout(HWND h) {
  TASKDIALOGCONFIG cfg{};
  cfg.cbSize = sizeof(cfg);
  cfg.hwndParent = h;
  cfg.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION;
  cfg.dwCommonButtons = TDCBF_OK_BUTTON;
  cfg.pszWindowTitle = L"About PortalVNC Agent Manager";
  cfg.pszMainIcon = TD_INFORMATION_ICON;
  cfg.pszMainInstruction = L"PortalVNC Agent Manager";
  cfg.pszContent =
      L"Version " PVNC_VERSION
      L"\n\nCentral certificate authority and agent configuration for PortalVNC "
      L"secure remote access."
      L"\n\nDeveloped by iSmartyPro ® — "
      L"<a href=\"https://www.ismarty.pro\">www.ismarty.pro</a>";
  cfg.pfCallback = aboutCallback;
  if (FAILED(TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr))) {
    // Fallback if the task dialog is unavailable.
    MessageBoxW(h,
                L"PortalVNC Agent Manager\nVersion " PVNC_VERSION
                L"\n\nDeveloped by iSmartyPro ®  www.ismarty.pro",
                L"About", MB_ICONINFORMATION | MB_OK);
  }
}

LRESULT CALLBACK mainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto* app = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (m) {
    case WM_CREATE: {
      auto* cs = (CREATESTRUCTW*)l;
      app = (App*)cs->lpCreateParams;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)app);
      HINSTANCE inst = cs->hInstance;
      app->tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0,
                                  0, kClientW, kTabH, h, nullptr, inst, nullptr);
      SendMessageW(app->tabs, WM_SETFONT, (WPARAM)uiFont(), TRUE);
      TCITEMW it{};
      it.mask = TCIF_TEXT;
      it.pszText = const_cast<wchar_t*>(L"Agent");
      TabCtrl_InsertItem(app->tabs, kTabAgent, &it);
      it.pszText = const_cast<wchar_t*>(L"Certificate Authority");
      TabCtrl_InsertItem(app->tabs, kTabCa, &it);

      // Menu bar: Help > About (shows the version).
      HMENU menu = CreateMenu();
      HMENU help = CreatePopupMenu();
      AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&About\tVersion " PVNC_VERSION);
      AppendMenuW(menu, MF_POPUP, (UINT_PTR)help, L"&Help");
      SetMenu(h, menu);

      app->ca.create(h);
      app->agent.create(h);
      layout(h, app);
      selectTab(app, kTabAgent);
      return 0;
    }
    case WM_NOTIFY: {
      auto* nm = (LPNMHDR)l;
      if (nm->hwndFrom == app->tabs && nm->code == TCN_SELCHANGE)
        selectTab(app, TabCtrl_GetCurSel(app->tabs));
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(w) == IDM_ABOUT) showAbout(h);
      return 0;
    case WM_SIZE:
      if (app) layout(h, app);
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mmi = (MINMAXINFO*)l;
      mmi->ptMinTrackSize.x = 700;
      mmi->ptMinTrackSize.y = 560;
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

}  // namespace
}  // namespace pvnc

#ifndef PVNC_VERSION
#define PVNC_VERSION "0.0.0"
#endif

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  using namespace pvnc;
  // STA COM is required for the shell file/folder dialogs and the object picker;
  // without it they fail with "no locations found".
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);

  WNDCLASSW wc{};
  wc.lpfnWndProc = mainProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
  wc.hbrBackground = windowBrush();
  wc.lpszClassName = L"PvncAdminMain";
  RegisterClassW(&wc);

  App app;
  RECT r{0, 0, kClientW, kClientH};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  int ww = r.right - r.left, wh = r.bottom - r.top;
  int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
  int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
  HWND h = CreateWindowExW(0, L"PvncAdminMain", L"PortalVNC Agent Manager  " PVNC_VERSION,
                           WS_OVERLAPPEDWINDOW, sx, sy, ww, wh, nullptr, nullptr, inst, &app);
  if (!h) {
    CoUninitialize();
    return 1;
  }
  ShowWindow(h, show);
  UpdateWindow(h);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(h, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  CoUninitialize();
  return 0;
}
