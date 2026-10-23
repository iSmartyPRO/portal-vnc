#include "files_dialog.h"

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace pvnc {

namespace {
using nlohmann::json;

enum {
  IDC_LLIST = 10, IDC_RLIST = 11, IDC_LPATH = 12, IDC_RPATH = 13,
  IDC_LUP = 14, IDC_RUP = 15, IDC_UPLOAD = 16, IDC_DOWNLOAD = 17,
  IDC_REFRESH = 18, IDC_CLOSE = 19,
};
const wchar_t* kClass = L"PortalVncFilesWnd";

struct Entry { std::wstring name; bool dir; double size; };
struct Pane { bool remote; std::wstring path; std::vector<Entry> entries; HWND list = nullptr, pathEdit = nullptr; };
struct State { RfbClient* client = nullptr; Pane local, remote; int active = 0; HWND status = nullptr; };

std::wstring toW(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n); return w;
}
std::wstring sizeStr(double bytes) {
  const wchar_t* u[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
  int i = 0; while (bytes >= 1024 && i < 4) { bytes /= 1024; ++i; }
  wchar_t b[32]; swprintf(b, 32, i == 0 ? L"%.0f %s" : L"%.1f %s", bytes, u[i]); return b;
}
std::wstring joinPath(const std::wstring& base, const std::wstring& name) {
  if (base.empty()) return name;
  std::wstring r = base;
  if (r.back() != L'\\' && r.back() != L'/') r += L'\\';
  return r + name;
}
std::wstring parentPath(const std::wstring& in) {
  if (in.empty()) return in;
  std::wstring s = in;
  while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
  auto pos = s.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return L"";
  return s.substr(0, pos + 1);
}

std::vector<Entry> listLocal(const std::wstring& path) {
  std::vector<Entry> out;
  if (path.empty()) {
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
      if (mask & (1u << i)) { wchar_t d[4] = {wchar_t(L'A' + i), L':', L'\\', 0}; out.push_back({d, true, 0}); }
    return out;
  }
  WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(joinPath(path, L"*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    std::wstring n = fd.cFileName;
    if (n == L"." || n == L"..") continue;
    bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out.push_back({n, dir, dir ? 0.0 : (double)((uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow)});
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return out;
}
std::vector<Entry> parseEntries(const std::string& js) {
  std::vector<Entry> out;
  json j; try { j = json::parse(js); } catch (...) { return out; }
  if (j.contains("entries"))
    for (const auto& e : j["entries"])
      out.push_back({toW(e.value("name", "")), e.value("dir", false), e.value("size", 0.0)});
  return out;
}
std::vector<Entry> listRemote(RfbClient* c, const std::wstring& path) {
  std::string js;
  return c->fileList(path, js) ? parseEntries(js) : std::vector<Entry>{};
}

void fillPane(Pane& p, RfbClient* c) {
  SetWindowTextW(p.pathEdit, p.path.c_str());
  ListView_DeleteAllItems(p.list);
  p.entries = p.remote ? listRemote(c, p.path) : listLocal(p.path);
  int idx = 0;
  for (const auto& e : p.entries) {
    LVITEMW it{}; it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = idx; it.lParam = idx;
    std::wstring nm = e.dir ? L"[" + e.name + L"]" : e.name;
    it.pszText = const_cast<wchar_t*>(nm.c_str());
    ListView_InsertItem(p.list, &it);
    if (!e.dir) ListView_SetItemText(p.list, idx, 1, const_cast<wchar_t*>(sizeStr(e.size).c_str()));
    ++idx;
  }
}

// Recursively copies one item (file or folder) using one shared control
// session. src pane is remote? -> dst is local, and vice versa.
bool copyItem(ControlSession& sess, bool srcRemote, const std::wstring& srcFull, bool isDir,
              const std::wstring& dstFull, int depth = 0) {
  if (depth > 64) return false;  // guard against junction/symlink loops
  if (!isDir) {
    return srcRemote ? sess.download(srcFull, dstFull) : sess.upload(srcFull, dstFull);
  }
  // Create destination directory.
  if (srcRemote) {  // dest is local
    int rc = SHCreateDirectoryExW(nullptr, dstFull.c_str(), nullptr);
    if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) return false;
  } else if (!sess.mkdir(dstFull)) {  // dest is remote
    return false;
  }
  std::vector<Entry> kids;
  if (srcRemote) { std::string js; if (sess.list(srcFull, js)) kids = parseEntries(js); }
  else kids = listLocal(srcFull);
  bool ok = true;
  for (const auto& k : kids)
    if (!copyItem(sess, srcRemote, joinPath(srcFull, k.name), k.dir, joinPath(dstFull, k.name), depth + 1)) ok = false;
  return ok;
}

void doCopy(HWND h, State* st, bool srcRemote) {
  Pane& src = srcRemote ? st->remote : st->local;
  Pane& dst = srcRemote ? st->local : st->remote;
  if (dst.path.empty()) { MessageBoxW(h, L"Open a destination folder on the other side first.", L"PortalVNC", MB_ICONINFORMATION); return; }
  if (src.path.empty()) { MessageBoxW(h, L"Open a source folder first.", L"PortalVNC", MB_ICONINFORMATION); return; }

  std::vector<int> sel;
  int i = -1;
  while ((i = ListView_GetNextItem(src.list, i, LVNI_SELECTED)) >= 0) {
    LVITEMW it{}; it.mask = LVIF_PARAM; it.iItem = i; ListView_GetItem(src.list, &it);
    sel.push_back((int)it.lParam);
  }
  if (sel.empty()) { MessageBoxW(h, L"Select files or folders to copy.", L"PortalVNC", MB_ICONINFORMATION); return; }

  SetWindowTextW(st->status, L"Copying...");
  SetCursor(LoadCursor(nullptr, IDC_WAIT));
  // One control connection for the whole batch (avoids a handshake + logon per file).
  ControlSession sess;
  if (!st->client->openControl(sess)) {
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    SetWindowTextW(st->status, L"Could not open a control connection.");
    return;
  }
  int okN = 0, failN = 0;
  for (int idx : sel) {
    const Entry& e = src.entries[idx];
    bool ok = copyItem(sess, srcRemote, joinPath(src.path, e.name), e.dir, joinPath(dst.path, e.name));
    ok ? ++okN : ++failN;
  }
  SetCursor(LoadCursor(nullptr, IDC_ARROW));
  fillPane(dst, st->client);
  wchar_t msg[128]; swprintf(msg, 128, L"Copied %d item(s)%s", okN, failN ? L", some failed" : L".");
  SetWindowTextW(st->status, msg);
}

void navigate(State* st, Pane& p) {
  int sel = ListView_GetNextItem(p.list, -1, LVNI_SELECTED | LVNI_FOCUSED);
  if (sel < 0) sel = ListView_GetNextItem(p.list, -1, LVNI_SELECTED);
  if (sel < 0) return;
  LVITEMW it{}; it.mask = LVIF_PARAM; it.iItem = sel; ListView_GetItem(p.list, &it);
  const Entry& e = p.entries[(int)it.lParam];
  if (e.dir) { p.path = joinPath(p.path, e.name); fillPane(p, st->client); }
}

HWND makeList(HWND h, HINSTANCE inst, int id, int x, int y, int w, int hh) {
  HWND lv = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_TABSTOP,
                            x, y, w, hh, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
  ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT);
  LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH;
  c.pszText = (LPWSTR)L"Name"; c.cx = w - 130; ListView_InsertColumn(lv, 0, &c);
  c.pszText = (LPWSTR)L"Size"; c.cx = 100; ListView_InsertColumn(lv, 1, &c);
  return lv;
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto* st = reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  switch (m) {
    case WM_CREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
      st = reinterpret_cast<State*>(cs->lpCreateParams);
      SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      HINSTANCE inst = cs->hInstance;
      const int paneW = 430, listH = 380, topY = 30, x1 = 10, x2 = 480;
      CreateWindowExW(0, L"STATIC", L"This computer (local)", WS_CHILD | WS_VISIBLE, x1, 8, 260, 18, h, nullptr, inst, nullptr);
      CreateWindowExW(0, L"STATIC", L"Remote computer", WS_CHILD | WS_VISIBLE, x2, 8, 260, 18, h, nullptr, inst, nullptr);
      st->local.pathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL, x1, topY, paneW - 56, 22, h, reinterpret_cast<HMENU>(IDC_LPATH), inst, nullptr);
      CreateWindowExW(0, L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE, x1 + paneW - 50, topY, 50, 22, h, reinterpret_cast<HMENU>(IDC_LUP), inst, nullptr);
      st->remote.pathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL, x2, topY, paneW - 56, 22, h, reinterpret_cast<HMENU>(IDC_RPATH), inst, nullptr);
      CreateWindowExW(0, L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE, x2 + paneW - 50, topY, 50, 22, h, reinterpret_cast<HMENU>(IDC_RUP), inst, nullptr);
      st->local.list = makeList(h, inst, IDC_LLIST, x1, topY + 28, paneW, listH);
      st->remote.list = makeList(h, inst, IDC_RLIST, x2, topY + 28, paneW, listH);

      int by = topY + 28 + listH + 10;
      CreateWindowExW(0, L"BUTTON", L"Upload \x2192", WS_CHILD | WS_VISIBLE, x1, by, 120, 30, h, reinterpret_cast<HMENU>(IDC_UPLOAD), inst, nullptr);
      CreateWindowExW(0, L"BUTTON", L"\x2190 Download", WS_CHILD | WS_VISIBLE, x1 + 128, by, 120, 30, h, reinterpret_cast<HMENU>(IDC_DOWNLOAD), inst, nullptr);
      st->status = CreateWindowExW(0, L"STATIC", L"Select items, then Upload/Download or press F5 to copy to the other panel.",
                                   WS_CHILD | WS_VISIBLE, x1 + 260, by + 7, 460, 18, h, nullptr, inst, nullptr);
      CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, x2 + paneW - 90, by, 90, 30, h, reinterpret_cast<HMENU>(IDC_CLOSE), inst, nullptr);

      // Start local at the user's profile, remote at drive list.
      wchar_t home[MAX_PATH] = L""; SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, home);
      st->local.path = home; st->remote.path = L"";
      fillPane(st->local, st->client);
      fillPane(st->remote, st->client);
      return 0;
    }
    case WM_NOTIFY: {
      auto* nh = reinterpret_cast<NMHDR*>(l);
      bool isLocal = (nh->idFrom == IDC_LLIST);
      bool isRemote = (nh->idFrom == IDC_RLIST);
      if (isLocal || isRemote) {
        Pane& p = isRemote ? st->remote : st->local;
        if (nh->code == NM_SETFOCUS || nh->code == NM_CLICK) st->active = isRemote ? 1 : 0;
        if (nh->code == NM_DBLCLK) navigate(st, p);
        if (nh->code == LVN_KEYDOWN) {
          auto* kd = reinterpret_cast<NMLVKEYDOWN*>(l);
          if (kd->wVKey == VK_F5) doCopy(h, st, st->active == 1);
          else if (kd->wVKey == VK_BACK) { p.path = parentPath(p.path); fillPane(p, st->client); }
          else if (kd->wVKey == VK_RETURN) navigate(st, p);
        }
      }
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case IDC_LUP: st->local.path = parentPath(st->local.path); fillPane(st->local, st->client); break;
        case IDC_RUP: st->remote.path = parentPath(st->remote.path); fillPane(st->remote, st->client); break;
        case IDC_UPLOAD: doCopy(h, st, false); break;    // local -> remote
        case IDC_DOWNLOAD: doCopy(h, st, true); break;    // remote -> local
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
  State st; st.client = &client; st.local.remote = false; st.remote.remote = true;
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc; wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
  wc.lpszClassName = kClass;
  RegisterClassW(&wc);

  RECT r{0, 0, 924, 520};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  int ww = r.right - r.left, hh = r.bottom - r.top;
  int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, sy = (GetSystemMetrics(SM_CYSCREEN) - hh) / 2;
  HWND dlg = CreateWindowExW(0, kClass, L"PortalVNC — File manager", WS_OVERLAPPEDWINDOW,
                             sx, sy, ww, hh, parent, nullptr, inst, &st);
  ShowWindow(dlg, SW_SHOW);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
}

}  // namespace pvnc
