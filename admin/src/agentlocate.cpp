#include "agentlocate.h"

#include <windows.h>

#include <winsvc.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "fileio.h"  // utf8ToW

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "version.lib")

namespace pvnc {

namespace {

// Extracts the executable path from an SCM ImagePath (may be quoted and may
// carry trailing arguments, e.g. `"C:\...\pvnc-agent.exe" --service`).
std::wstring exeFromImagePath(const std::wstring& image) {
  std::wstring s = image;
  size_t i = 0;
  while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) ++i;
  if (i < s.size() && s[i] == L'"') {
    size_t end = s.find(L'"', i + 1);
    if (end != std::wstring::npos) return s.substr(i + 1, end - (i + 1));
    return s.substr(i + 1);
  }
  size_t sp = s.find(L' ', i);
  return (sp == std::wstring::npos) ? s.substr(i) : s.substr(i, sp - i);
}

std::wstring parentDir(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  return (slash == std::wstring::npos) ? std::wstring() : path.substr(0, slash);
}

// File version "a.b.c" (drops a trailing .0 build) via GetFileVersionInfo.
std::wstring fileVersion(const std::wstring& exePath) {
  if (exePath.empty()) return {};
  DWORD handle = 0;
  DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
  if (size == 0) return {};
  std::vector<uint8_t> buf(size);
  if (!GetFileVersionInfoW(exePath.c_str(), 0, size, buf.data())) return {};
  VS_FIXEDFILEINFO* ffi = nullptr;
  UINT len = 0;
  if (!VerQueryValueW(buf.data(), L"\\", (void**)&ffi, &len) || !ffi) return {};
  unsigned a = HIWORD(ffi->dwFileVersionMS), b = LOWORD(ffi->dwFileVersionMS);
  unsigned c = HIWORD(ffi->dwFileVersionLS), d = LOWORD(ffi->dwFileVersionLS);
  wchar_t out[48];
  if (d != 0)
    swprintf(out, 48, L"%u.%u.%u.%u", a, b, c, d);
  else
    swprintf(out, 48, L"%u.%u.%u", a, b, c);
  return out;
}

std::string readFile(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

std::wstring defaultConfigDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetEnvironmentVariableW(L"ProgramData", buf, MAX_PATH);
  std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : L"C:\\ProgramData";
  return base + L"\\PortalVNC";
}

AgentInstall locateAgent() {
  AgentInstall info;
  info.configDir = defaultConfigDir();
  info.state = queryService(kServiceName);

  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (scm) {
    SC_HANDLE svc = OpenServiceW(scm, kServiceName, SERVICE_QUERY_CONFIG);
    if (svc) {
      info.installed = true;
      DWORD need = 0;
      QueryServiceConfigW(svc, nullptr, 0, &need);  // size probe
      if (need) {
        std::vector<uint8_t> buf(need);
        auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
        if (QueryServiceConfigW(svc, cfg, need, &need) && cfg->lpBinaryPathName) {
          info.exePath = exeFromImagePath(cfg->lpBinaryPathName);
          std::wstring dir = parentDir(info.exePath);
          if (!dir.empty()) info.configDir = dir;
        }
      }
      CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
  }
  info.exeVersion = fileVersion(info.exePath);
  return info;
}

LiveStatus aggregateStatus(const std::wstring& dir) {
  LiveStatus live;
  std::wstring pattern = dir + L"\\status-*.json";
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return live;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    std::string text = readFile(dir + L"\\" + fd.cFileName);
    if (text.empty()) continue;
    nlohmann::json j;
    try {
      j = nlohmann::json::parse(text);
    } catch (...) {
      continue;  // skip malformed status files
    }
    if (live.agentVersion.empty() && j.contains("agentVersion"))
      live.agentVersion = utf8ToW(j.value("agentVersion", std::string()));
    if (j.contains("port") && j["port"].is_number_unsigned())
      live.ports.push_back((uint16_t)j["port"].get<unsigned>());
    if (j.contains("sessions") && j["sessions"].is_array()) {
      for (const auto& e : j["sessions"]) {
        SessionInfo s;
        s.user = utf8ToW(e.value("user", std::string()));
        s.ip = e.value("ip", std::string());
        s.mode = e.value("mode", std::string());
        s.sinceUnix = e.value("since", (uint64_t)0);
        live.sessions.push_back(std::move(s));
      }
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(live.ports.begin(), live.ports.end());
  return live;
}

}  // namespace pvnc
