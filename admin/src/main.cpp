#include <windows.h>

#include <commctrl.h>

#include "resource.h"
#include "ui_agent.h"
#include "ui_ca.h"
#include "ui_common.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace pvnc {
namespace {

constexpr int kTabH = 28;
constexpr int kClientW = 760;
constexpr int kClientH = 700;

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
  MoveWindow(app->ca.hwnd(), 0, kTabH, w, ht - kTabH, TRUE);
  MoveWindow(app->agent.hwnd(), 0, kTabH, w, ht - kTabH, TRUE);
}

void selectTab(App* app, int idx) {
  ShowWindow(app->ca.hwnd(), idx == 0 ? SW_SHOW : SW_HIDE);
  ShowWindow(app->agent.hwnd(), idx == 1 ? SW_SHOW : SW_HIDE);
  if (idx == 1) app->agent.onShow();
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
      SendMessageW(app->tabs, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
      TCITEMW it{};
      it.mask = TCIF_TEXT;
      it.pszText = const_cast<wchar_t*>(L"Certificate Authority");
      TabCtrl_InsertItem(app->tabs, 0, &it);
      it.pszText = const_cast<wchar_t*>(L"Agent");
      TabCtrl_InsertItem(app->tabs, 1, &it);

      app->ca.create(h);
      app->agent.create(h);
      layout(h, app);
      selectTab(app, 0);
      return 0;
    }
    case WM_NOTIFY: {
      auto* nm = (LPNMHDR)l;
      if (nm->hwndFrom == app->tabs && nm->code == TCN_SELCHANGE)
        selectTab(app, TabCtrl_GetCurSel(app->tabs));
      return 0;
    }
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

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  using namespace pvnc;
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
  HWND h = CreateWindowExW(0, L"PvncAdminMain", L"PortalVNC Agent Manager", WS_OVERLAPPEDWINDOW, sx,
                           sy, ww, wh, nullptr, nullptr, inst, &app);
  if (!h) return 1;
  enableDarkTitleBar(h);
  ShowWindow(h, show);
  UpdateWindow(h);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(h, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return 0;
}
