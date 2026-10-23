#include "sysinfo_dialog.h"

#include <windows.h>

#include <nlohmann/json.hpp>

#include "resource.h"

namespace pvnc {

namespace {
using nlohmann::json;

std::wstring toW(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
std::string getStr(const json& j, const char* k) {
  return (j.contains(k) && j[k].is_string()) ? j[k].get<std::string>() : std::string();
}

std::wstring format(const std::string& jsonText) {
  std::wstring out;
  auto line = [&](const std::wstring& label, const std::wstring& val) {
    out += label; out += L"\t"; out += val; out += L"\r\n";
  };
  json j;
  try { j = json::parse(jsonText); }
  catch (...) { return L"Could not parse system information."; }

  std::wstring host = toW(getStr(j, "host"));
  std::wstring domain = toW(getStr(j, "domain"));
  bool joined = j.value("domainJoined", false);
  line(L"Computer:", host + (domain.empty() ? L"" : (joined ? L"   (domain: " : L"   (workgroup: ") + domain + L")"));
  line(L"User:", toW(getStr(j, "user")));
  if (j.contains("os")) {
    const auto& o = j["os"];
    std::wstring os = toW(getStr(o, "name"));
    std::wstring rel = toW(getStr(o, "release"));
    std::wstring ver = toW(getStr(o, "version"));
    if (!rel.empty()) os += L" " + rel;
    if (!ver.empty()) os += L" (" + ver + L")";
    line(L"OS:", os);
  }
  if (j.contains("system")) {
    const auto& s = j["system"];
    std::wstring man = toW(getStr(s, "manufacturer")), mod = toW(getStr(s, "model"));
    if (!man.empty() || !mod.empty()) line(L"System:", (man + L" " + mod));
  }
  if (j.contains("cpu")) {
    const auto& c = j["cpu"];
    std::wstring cpu = toW(getStr(c, "name"));
    if (c.contains("logical")) cpu += L"  (" + std::to_wstring(c["logical"].get<int>()) + L" logical, " + toW(getStr(c, "arch")) + L")";
    line(L"CPU:", cpu);
  }
  if (j.contains("memory")) {
    const auto& m = j["memory"];
    wchar_t b[128];
    swprintf(b, 128, L"%.0f MB free of %.0f MB  (%d%% used)",
             m.value("availMB", 0.0), m.value("totalMB", 0.0), m.value("usedPercent", 0));
    line(L"Memory:", b);
  }
  line(L"Uptime:", toW(getStr(j, "uptime")));

  if (j.contains("disks") && j["disks"].is_array() && !j["disks"].empty()) {
    out += L"\r\nDisks:\r\n";
    for (const auto& d : j["disks"]) {
      wchar_t b[160];
      swprintf(b, 160, L"   %S  %.1f GB free of %.1f GB  (%S)\r\n",
               getStr(d, "drive").c_str(), d.value("freeGB", 0.0), d.value("totalGB", 0.0),
               getStr(d, "type").c_str());
      out += b;
    }
  }
  if (j.contains("network") && j["network"].is_array() && !j["network"].empty()) {
    out += L"\r\nNetwork:\r\n";
    for (const auto& n : j["network"]) {
      std::wstring ips;
      if (n.contains("ipv4"))
        for (const auto& ip : n["ipv4"]) { if (!ips.empty()) ips += L", "; ips += toW(ip.get<std::string>()); }
      out += L"   " + toW(getStr(n, "adapter")) + L":  " + ips + L"\r\n";
    }
  }
  return out;
}

const wchar_t* kClass = L"PortalVncSysInfoWnd";
std::wstring* g_text = nullptr;

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      HWND edit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", g_text ? g_text->c_str() : L"",
          WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
          10, 10, 520, 360, h, reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
      SendMessageW(edit, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), TRUE);
      CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                      450, 378, 80, 26, h, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
      return 0;
    }
    case WM_SIZE: {
      HWND edit = GetDlgItem(h, 1);
      if (edit) MoveWindow(edit, 10, 10, LOWORD(l) - 20, HIWORD(l) - 50, TRUE);
      HWND btn = GetDlgItem(h, IDCANCEL);
      if (btn) MoveWindow(btn, LOWORD(l) - 90, HIWORD(l) - 36, 80, 26, TRUE);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(w) == IDCANCEL) DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

void showSystemInfo(HWND parent, const std::string& jsonText) {
  std::wstring text = format(jsonText);
  g_text = &text;
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
  wc.lpszClassName = kClass;
  RegisterClassW(&wc);

  RECT r{0, 0, 560, 430};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  int ww = r.right - r.left, hh = r.bottom - r.top;
  int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, sy = (GetSystemMetrics(SM_CYSCREEN) - hh) / 2;
  HWND dlg = CreateWindowExW(0, kClass, L"Remote computer information",
                             WS_OVERLAPPEDWINDOW, sx, sy, ww, hh, parent, nullptr, inst, nullptr);
  ShowWindow(dlg, SW_SHOW);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
  g_text = nullptr;
}

}  // namespace pvnc
