#include "connect_dialog.h"

#include <windows.h>

#include "resource.h"

namespace pvnc {

namespace {
constexpr int IDC_HOST = 101;
constexpr int IDC_PORT = 102;
constexpr int IDC_USER = 103;
constexpr int IDC_PASS = 104;
constexpr int IDC_CONNECT = IDOK;
constexpr int IDC_CANCEL = IDCANCEL;
const wchar_t* kClass = L"PortalVncConnectDlg";

struct DlgState {
  ConnectInfo* io = nullptr;
  bool connect = false;
  bool done = false;
  HWND host = nullptr, port = nullptr, user = nullptr, pass = nullptr;
};

std::wstring getText(HWND edit) {
  int n = GetWindowTextLengthW(edit);
  std::wstring s(n, L'\0');
  if (n) GetWindowTextW(edit, s.data(), n + 1);
  return s;
}

HWND makeLabel(HWND parent, HINSTANCE inst, const wchar_t* text, int x, int y) {
  return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, 80, 20, parent,
                         nullptr, inst, nullptr);
}
HWND makeEdit(HWND parent, HINSTANCE inst, int id, int x, int y, int w, DWORD extra) {
  return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra, x, y, w, 24,
                         parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
}

void applyFont(HWND w) {
  HFONT f = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  EnumChildWindows(w, [](HWND c, LPARAM lp) -> BOOL {
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(reinterpret_cast<HFONT>(lp)), TRUE);
    return TRUE;
  }, reinterpret_cast<LPARAM>(f));
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto* st = reinterpret_cast<DlgState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  switch (m) {
    case WM_CREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
      st = reinterpret_cast<DlgState*>(cs->lpCreateParams);
      SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      HINSTANCE inst = cs->hInstance;
      const int lx = 16, ex = 110, ew = 230;
      makeLabel(h, inst, L"Host", lx, 22);
      st->host = makeEdit(h, inst, IDC_HOST, ex, 18, ew, 0);
      makeLabel(h, inst, L"Port", lx, 56);
      st->port = makeEdit(h, inst, IDC_PORT, ex, 52, 70, ES_NUMBER);
      makeLabel(h, inst, L"User", lx, 90);
      st->user = makeEdit(h, inst, IDC_USER, ex, 86, ew, 0);
      makeLabel(h, inst, L"Password", lx, 124);
      st->pass = makeEdit(h, inst, IDC_PASS, ex, 120, ew, ES_PASSWORD);
      CreateWindowExW(0, L"BUTTON", L"Connect", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                      ex + ew - 170, 162, 80, 28, h,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONNECT)), inst, nullptr);
      CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      ex + ew - 80, 162, 80, 28, h,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CANCEL)), inst, nullptr);

      // Prefill
      SetWindowTextW(st->host, st->io->host.c_str());
      SetWindowTextW(st->port, std::to_wstring(st->io->port).c_str());
      SetWindowTextW(st->user, st->io->user.c_str());
      SetWindowTextW(st->pass, st->io->password.c_str());
      applyFont(h);
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(w);
      if (id == IDC_CONNECT) {
        st->io->host = getText(st->host);
        st->io->port = static_cast<uint16_t>(_wtoi(getText(st->port).c_str()));
        if (st->io->port == 0) st->io->port = 7654;
        st->io->user = getText(st->user);
        st->io->password = getText(st->pass);
        if (st->io->host.empty() || st->io->user.empty()) {
          MessageBoxW(h, L"Enter at least a host and a user name.", L"PortalVNC", MB_ICONWARNING);
          return 0;
        }
        st->connect = true; st->done = true;
        DestroyWindow(h);
        return 0;
      }
      if (id == IDC_CANCEL) { st->connect = false; st->done = true; DestroyWindow(h); return 0; }
      return 0;
    }
    case WM_CLOSE:
      st->connect = false; st->done = true; DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

bool showConnectDialog(ConnectInfo& io) {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = kClass;
  RegisterClassW(&wc);

  DlgState st;
  st.io = &io;

  RECT r{0, 0, 380, 210};
  AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
  int w = r.right - r.left, hgt = r.bottom - r.top;
  int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, sy = (GetSystemMetrics(SM_CYSCREEN) - hgt) / 2;
  HWND dlg = CreateWindowExW(0, kClass, L"PortalVNC — Connect",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, sx, sy, w, hgt, nullptr,
                             nullptr, inst, &st);
  if (!dlg) return false;
  ShowWindow(dlg, SW_SHOW);
  SetForegroundWindow(dlg);
  // Set keyboard focus AFTER the window is shown/active (SetFocus in WM_CREATE,
  // before the window is visible, does not stick — which left typed input going
  // nowhere). Focus the password field when the user is prefilled, else user.
  SetFocus(io.user.empty() ? st.user : st.pass);

  MSG msg;
  while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return st.connect;
}

}  // namespace pvnc
