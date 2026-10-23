#include "ui_common.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shlobj.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace pvnc {

HBRUSH windowBrush() {
  static HBRUSH b = CreateSolidBrush(theme::kWindow);
  return b;
}
HBRUSH panelBrush() {
  static HBRUSH b = CreateSolidBrush(theme::kPanel);
  return b;
}
HBRUSH editBrush() {
  static HBRUSH b = CreateSolidBrush(theme::kEdit);
  return b;
}

void enableDarkTitleBar(HWND hwnd) {
  BOOL dark = TRUE;
  // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (1903+); older builds used 19.
  if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

LRESULT darkCtlColor(UINT msg, WPARAM wParam, bool& handled) {
  handled = true;
  HDC dc = (HDC)wParam;
  switch (msg) {
    case WM_CTLCOLORSTATIC:
      SetTextColor(dc, theme::kText);
      SetBkColor(dc, theme::kPanel);
      return (LRESULT)panelBrush();
    case WM_CTLCOLORBTN:
      SetBkColor(dc, theme::kPanel);
      return (LRESULT)panelBrush();
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
      SetTextColor(dc, theme::kText);
      SetBkColor(dc, theme::kEdit);
      return (LRESULT)editBrush();
  }
  handled = false;
  return 0;
}

void applyGuiFont(HWND parent) {
  HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  EnumChildWindows(
      parent,
      [](HWND c, LPARAM lp) -> BOOL {
        SendMessageW(c, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE);
        return TRUE;
      },
      (LPARAM)f);
}

static HMENU idMenu(int id) { return (HMENU)(INT_PTR)id; }

// ---------------- themed panel window ----------------
namespace {
struct PanelBinding {
  void* owner = nullptr;
  PanelCommandFn fn = nullptr;
};
LRESULT CALLBACK panelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto* b = (PanelBinding*)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (m) {
    case WM_ERASEBKGND: {
      RECT rc;
      GetClientRect(h, &rc);
      FillRect((HDC)w, &rc, windowBrush());
      return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      bool handled = false;
      LRESULT r = darkCtlColor(m, w, handled);
      if (handled) return r;
      break;
    }
    case WM_COMMAND:
      if (b && b->fn) b->fn(b->owner, LOWORD(w));
      return 0;
    case WM_NCDESTROY:
      delete b;
      SetWindowLongPtrW(h, GWLP_USERDATA, 0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

HWND createPanelWindow(HWND parent, void* owner, PanelCommandFn fn) {
  static bool reg = false;
  HINSTANCE inst = GetModuleHandleW(nullptr);
  if (!reg) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = panelProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = windowBrush();
    wc.lpszClassName = L"PvncPanel";
    RegisterClassW(&wc);
    reg = true;
  }
  auto* b = new PanelBinding{owner, fn};
  HWND h = CreateWindowExW(0, L"PvncPanel", L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, parent,
                           nullptr, inst, nullptr);
  SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)b);
  return h;
}

HWND makeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
  return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr,
                         GetModuleHandleW(nullptr), nullptr);
}
HWND makeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
  return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, w, h, parent,
                         idMenu(id), GetModuleHandleW(nullptr), nullptr);
}
HWND makeEdit(HWND parent, int id, int x, int y, int w, int h, DWORD extra) {
  return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra, x, y, w, h,
                         parent, idMenu(id), GetModuleHandleW(nullptr), nullptr);
}
HWND makeGroup(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
  return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, x, y, w, h, parent,
                         nullptr, GetModuleHandleW(nullptr), nullptr);
}
HWND makeCheckbox(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
  return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x,
                         y, w, h, parent, idMenu(id), GetModuleHandleW(nullptr), nullptr);
}
HWND makeListView(HWND parent, int id, int x, int y, int w, int h) {
  HWND lv = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, x, y,
                            w, h, parent, idMenu(id), GetModuleHandleW(nullptr), nullptr);
  ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
  ListView_SetBkColor(lv, theme::kEdit);
  ListView_SetTextBkColor(lv, theme::kEdit);
  ListView_SetTextColor(lv, theme::kText);
  return lv;
}

void lvAddColumn(HWND lv, int index, const wchar_t* title, int width) {
  LVCOLUMNW col{};
  col.mask = LVCF_TEXT | LVCF_WIDTH;
  col.pszText = const_cast<wchar_t*>(title);
  col.cx = width;
  ListView_InsertColumn(lv, index, &col);
}
int lvAddRow(HWND lv, const std::wstring& col0, const std::vector<std::wstring>& more) {
  LVITEMW it{};
  it.mask = LVIF_TEXT;
  it.iItem = ListView_GetItemCount(lv);
  it.pszText = const_cast<wchar_t*>(col0.c_str());
  int row = ListView_InsertItem(lv, &it);
  for (size_t i = 0; i < more.size(); ++i)
    ListView_SetItemText(lv, row, (int)i + 1, const_cast<wchar_t*>(more[i].c_str()));
  return row;
}

std::wstring getWindowText(HWND w) {
  int n = GetWindowTextLengthW(w);
  std::wstring s(n, L'\0');
  if (n) GetWindowTextW(w, s.data(), n + 1);
  return s;
}

// ---------------- prompt dialog ----------------
namespace {
struct PromptState {
  std::vector<Field>* fields = nullptr;
  std::vector<HWND> edits;
  bool ok = false;
  bool done = false;
};
constexpr int kFirstEditId = 2001;

LRESULT CALLBACK promptProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto* st = (PromptState*)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (m) {
    case WM_CREATE: {
      auto* cs = (CREATESTRUCTW*)l;
      st = (PromptState*)cs->lpCreateParams;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
      int y = 14;
      int idx = 0;
      for (auto& f : *st->fields) {
        makeLabel(h, f.label.c_str(), 16, y + 2, 120, 20);
        HWND e = makeEdit(h, kFirstEditId + idx, 140, y, 240, 24, f.password ? ES_PASSWORD : 0);
        SetWindowTextW(e, f.value.c_str());
        st->edits.push_back(e);
        y += 34;
        ++idx;
      }
      makeButton(h, IDOK, L"OK", 220, y + 6, 76, 28);
      makeButton(h, IDCANCEL, L"Cancel", 304, y + 6, 76, 28);
      applyGuiFont(h);
      return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT: {
      bool handled = false;
      LRESULT r = darkCtlColor(m, w, handled);
      if (handled) return r;
      break;
    }
    case WM_COMMAND: {
      int id = LOWORD(w);
      if (id == IDOK) {
        for (size_t i = 0; i < st->edits.size(); ++i)
          (*st->fields)[i].value = getWindowText(st->edits[i]);
        st->ok = true;
        st->done = true;
        DestroyWindow(h);
        return 0;
      }
      if (id == IDCANCEL) {
        st->done = true;
        DestroyWindow(h);
        return 0;
      }
      return 0;
    }
    case WM_CLOSE:
      st->done = true;
      DestroyWindow(h);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

bool promptFields(HWND owner, const wchar_t* title, std::vector<Field>& fields) {
  static bool reg = false;
  HINSTANCE inst = GetModuleHandleW(nullptr);
  if (!reg) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = promptProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = panelBrush();
    wc.lpszClassName = L"PvncPromptDlg";
    RegisterClassW(&wc);
    reg = true;
  }
  PromptState st;
  st.fields = &fields;
  int rows = (int)fields.size();
  int hgt = 14 + rows * 34 + 48;
  RECT r{0, 0, 400, hgt};
  AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
  int ww = r.right - r.left, wh = r.bottom - r.top;
  RECT op{};
  GetWindowRect(owner ? owner : GetDesktopWindow(), &op);
  int sx = op.left + 60, sy = op.top + 60;
  HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PvncPromptDlg", title,
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, sx, sy, ww, wh, owner, nullptr,
                             inst, &st);
  if (!dlg) return false;
  enableDarkTitleBar(dlg);
  EnableWindow(owner, FALSE);
  ShowWindow(dlg, SW_SHOW);
  SetForegroundWindow(dlg);
  if (!st.edits.empty()) SetFocus(st.edits[0]);
  MSG msg;
  while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  EnableWindow(owner, TRUE);
  if (owner) SetForegroundWindow(owner);
  return st.ok;
}

bool promptText(HWND owner, const wchar_t* title, const wchar_t* label, std::wstring& value) {
  std::vector<Field> f{{label, value, false}};
  if (!promptFields(owner, title, f)) return false;
  value = f[0].value;
  return true;
}

bool pickFolder(HWND owner, const wchar_t* title, std::wstring& out) {
  BROWSEINFOW bi{};
  bi.hwndOwner = owner;
  bi.lpszTitle = title;
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
  if (!idl) return false;
  wchar_t path[MAX_PATH];
  bool ok = SHGetPathFromIDListW(idl, path);
  CoTaskMemFree(idl);
  if (ok) out = path;
  return ok;
}

static bool commonFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter,
                             const wchar_t* defName, bool save, std::wstring& out) {
  wchar_t buf[MAX_PATH] = {0};
  if (defName) wcsncpy_s(buf, defName, _TRUNCATE);
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = title;
  ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
  if (ok) out = buf;
  return ok != 0;
}

bool openFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter, std::wstring& out) {
  return commonFileDialog(owner, title, filter, nullptr, false, out);
}
bool saveFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter, const wchar_t* defName,
                    std::wstring& out) {
  return commonFileDialog(owner, title, filter, defName, true, out);
}

}  // namespace pvnc
