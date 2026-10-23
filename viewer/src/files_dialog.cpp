#include "files_dialog.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace pvnc {

namespace {
using nlohmann::json;

enum { IDC_PATH = 1, IDC_LIST = 2, IDC_UP = 3, IDC_GO = 4, IDC_DL = 5, IDC_UL = 6, IDC_CLOSE = 7 };
const wchar_t* kClass = L"PortalVncFilesWnd";

struct Entry { std::wstring name; bool dir; double size; };
struct State {
  RfbClient* client = nullptr;
  std::wstring path;  // empty = drive list
  std::vector<Entry> entries;
  HWND list = nullptr, pathEdit = nullptr;
};

std::wstring toW(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
std::wstring sizeStr(double bytes) {
  const wchar_t* u[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
  int i = 0; while (bytes >= 1024 && i < 4) { bytes /= 1024; ++i; }
  wchar_t b[32]; swprintf(b, 32, i == 0 ? L"%.0f %s" : L"%.1f %s", bytes, u[i]); return b;
}

void refresh(HWND h, State* st) {
  SetWindowTextW(st->pathEdit, st->path.c_str());
  ListView_DeleteAllItems(st->list);
  st->entries.clear();
  std::string jsonText;
  if (!st->client->fileList(st->path, jsonText)) {
    MessageBoxW(h, L"Could not list the remote folder.", L"PortalVNC", MB_ICONWARNING);
    return;
  }
  json j; try { j = json::parse(jsonText); } catch (...) { return; }
  if (j.contains("entries"))
    for (const auto& e : j["entries"])
      st->entries.push_back({toW(e.value("name", "")), e.value("dir", false), e.value("size", 0.0)});

  int idx = 0;
  for (const auto& e : st->entries) {
    LVITEMW it{}; it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = idx; it.lParam = idx;
    std::wstring nm = (e.dir ? L"[" + e.name + L"]" : e.name);
    it.pszText = const_cast<wchar_t*>(nm.c_str());
    ListView_InsertItem(st->list, &it);
    if (!e.dir) ListView_SetItemText(st->list, idx, 1, const_cast<wchar_t*>(sizeStr(e.size).c_str()));
    ListView_SetItemText(st->list, idx, 2, const_cast<wchar_t*>(e.dir ? L"Folder" : L"File"));
    ++idx;
  }
}

std::wstring joinPath(const std::wstring& base, const std::wstring& name) {
  if (base.empty()) return name;  // drive entries already end with a separator
  std::wstring r = base;
  if (r.back() != L'\\' && r.back() != L'/') r += L'\\';
  return r + name;
}
std::wstring parentPath(const std::wstring& in) {
  if (in.empty()) return in;
  std::wstring s = in;
  while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
  auto pos = s.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return L"";  // back to drive list
  std::wstring parent = s.substr(0, pos + 1);
  return parent;
}

int selectedEntry(State* st) {
  int sel = ListView_GetNextItem(st->list, -1, LVNI_SELECTED);
  if (sel < 0) return -1;
  LVITEMW it{}; it.mask = LVIF_PARAM; it.iItem = sel;
  ListView_GetItem(st->list, &it);
  return (int)it.lParam;
}

void doDownload(HWND h, State* st) {
  int e = selectedEntry(st);
  if (e < 0 || st->entries[e].dir) { MessageBoxW(h, L"Select a file to download.", L"PortalVNC", MB_ICONINFORMATION); return; }
  wchar_t out[MAX_PATH]; lstrcpynW(out, st->entries[e].name.c_str(), MAX_PATH);
  OPENFILENAMEW of{}; of.lStructSize = sizeof(of); of.hwndOwner = h;
  of.lpstrFile = out; of.nMaxFile = MAX_PATH; of.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&of)) return;
  SetCursor(LoadCursor(nullptr, IDC_WAIT));
  bool ok = st->client->fileDownload(joinPath(st->path, st->entries[e].name), out);
  SetCursor(LoadCursor(nullptr, IDC_ARROW));
  MessageBoxW(h, ok ? L"Download complete." : L"Download failed.", L"PortalVNC",
              ok ? MB_ICONINFORMATION : MB_ICONERROR);
}

void doUpload(HWND h, State* st) {
  if (st->path.empty()) { MessageBoxW(h, L"Open a folder first.", L"PortalVNC", MB_ICONINFORMATION); return; }
  wchar_t in[MAX_PATH] = {0};
  OPENFILENAMEW of{}; of.lStructSize = sizeof(of); of.hwndOwner = h;
  of.lpstrFile = in; of.nMaxFile = MAX_PATH; of.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&of)) return;
  std::wstring name = PathFindFileNameW(in);
  SetCursor(LoadCursor(nullptr, IDC_WAIT));
  bool ok = st->client->fileUpload(in, joinPath(st->path, name));
  SetCursor(LoadCursor(nullptr, IDC_ARROW));
  MessageBoxW(h, ok ? L"Upload complete." : L"Upload failed.", L"PortalVNC",
              ok ? MB_ICONINFORMATION : MB_ICONERROR);
  if (ok) refresh(h, st);
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto* st = reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  switch (m) {
    case WM_CREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
      st = reinterpret_cast<State*>(cs->lpCreateParams);
      SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      HINSTANCE inst = cs->hInstance;
      st->pathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                     8, 8, 420, 24, h, reinterpret_cast<HMENU>(IDC_PATH), inst, nullptr);
      CreateWindowExW(0, L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE, 436, 8, 50, 24, h,
                      reinterpret_cast<HMENU>(IDC_UP), inst, nullptr);
      CreateWindowExW(0, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE, 492, 8, 70, 24, h,
                      reinterpret_cast<HMENU>(IDC_GO), inst, nullptr);
      st->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                 8, 40, 554, 300, h, reinterpret_cast<HMENU>(IDC_LIST), inst, nullptr);
      ListView_SetExtendedListViewStyle(st->list, LVS_EX_FULLROWSELECT);
      LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH;
      c.pszText = (LPWSTR)L"Name"; c.cx = 340; ListView_InsertColumn(st->list, 0, &c);
      c.pszText = (LPWSTR)L"Size"; c.cx = 110; ListView_InsertColumn(st->list, 1, &c);
      c.pszText = (LPWSTR)L"Type"; c.cx = 90; ListView_InsertColumn(st->list, 2, &c);
      CreateWindowExW(0, L"BUTTON", L"Download", WS_CHILD | WS_VISIBLE, 8, 348, 90, 28, h,
                      reinterpret_cast<HMENU>(IDC_DL), inst, nullptr);
      CreateWindowExW(0, L"BUTTON", L"Upload", WS_CHILD | WS_VISIBLE, 104, 348, 90, 28, h,
                      reinterpret_cast<HMENU>(IDC_UL), inst, nullptr);
      CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 472, 348, 90, 28, h,
                      reinterpret_cast<HMENU>(IDC_CLOSE), inst, nullptr);
      refresh(h, st);
      return 0;
    }
    case WM_NOTIFY: {
      auto* nm = reinterpret_cast<NMHDR*>(l);
      if (nm->idFrom == IDC_LIST && nm->code == NM_DBLCLK) {
        int e = selectedEntry(st);
        if (e >= 0 && st->entries[e].dir) { st->path = joinPath(st->path, st->entries[e].name); refresh(h, st); }
      }
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case IDC_UP: st->path = parentPath(st->path); refresh(h, st); break;
        case IDC_GO: refresh(h, st); break;
        case IDC_DL: doDownload(h, st); break;
        case IDC_UL: doUpload(h, st); break;
        case IDC_CLOSE: DestroyWindow(h); break;
      }
      return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

void showFiles(HWND parent, RfbClient& client) {
  INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_LISTVIEW_CLASSES};
  InitCommonControlsEx(&ic);
  State st; st.client = &client;
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc; wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
  wc.lpszClassName = kClass;
  RegisterClassW(&wc);

  RECT r{0, 0, 580, 420};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  int ww = r.right - r.left, hh = r.bottom - r.top;
  int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, sy = (GetSystemMetrics(SM_CYSCREEN) - hh) / 2;
  HWND dlg = CreateWindowExW(0, kClass, L"Remote files", WS_OVERLAPPEDWINDOW, sx, sy, ww, hh,
                             parent, nullptr, inst, &st);
  ShowWindow(dlg, SW_SHOW);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
}

}  // namespace pvnc
