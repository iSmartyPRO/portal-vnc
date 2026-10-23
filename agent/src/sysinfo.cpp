#include "sysinfo.h"

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <lm.h>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "netapi32.lib")

namespace pvnc {

namespace {
using nlohmann::json;

std::string toUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring regStr(HKEY root, const wchar_t* sub, const wchar_t* name) {
  wchar_t buf[512]; DWORD cb = sizeof(buf);
  if (RegGetValueW(root, sub, name, RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS)
    return std::wstring(buf);
  return {};
}
DWORD regDword(HKEY root, const wchar_t* sub, const wchar_t* name) {
  DWORD v = 0, cb = sizeof(v);
  RegGetValueW(root, sub, name, RRF_RT_REG_DWORD, nullptr, &v, &cb);
  return v;
}

std::string computerName(COMPUTER_NAME_FORMAT fmt) {
  wchar_t buf[256]; DWORD n = 256;
  return GetComputerNameExW(fmt, buf, &n) ? toUtf8(std::wstring(buf, n)) : std::string();
}

std::string loggedOnUser() {
  wchar_t buf[256]; DWORD n = 256;
  return GetUserNameW(buf, &n) ? toUtf8(std::wstring(buf, n ? n - 1 : 0)) : std::string();
}

std::string uptimeStr() {
  unsigned long long ms = GetTickCount64();
  unsigned long long s = ms / 1000, d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
  char b[64];
  snprintf(b, sizeof(b), "%llud %lluh %llum", d, h, m);
  return b;
}

const char* driveTypeStr(UINT t) {
  switch (t) {
    case DRIVE_FIXED: return "fixed";
    case DRIVE_REMOVABLE: return "removable";
    case DRIVE_REMOTE: return "network";
    case DRIVE_CDROM: return "cdrom";
    case DRIVE_RAMDISK: return "ramdisk";
    default: return "other";
  }
}

json osInfo() {
  json o;
  // ProductName/build from the registry; real version via RtlGetVersion.
  const wchar_t* k = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  o["name"] = toUtf8(regStr(HKEY_LOCAL_MACHINE, k, L"ProductName"));
  std::wstring display = regStr(HKEY_LOCAL_MACHINE, k, L"DisplayVersion");
  if (!display.empty()) o["release"] = toUtf8(display);
  typedef LONG(WINAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
  RTL_OSVERSIONINFOW vi{}; vi.dwOSVersionInfoSize = sizeof(vi);
  HMODULE nt = GetModuleHandleW(L"ntdll.dll");
  auto rtl = nt ? reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(nt, "RtlGetVersion")) : nullptr;
  DWORD build = regDword(HKEY_LOCAL_MACHINE, k, L"CurrentBuildNumber");
  std::wstring buildStr = regStr(HKEY_LOCAL_MACHINE, k, L"CurrentBuild");
  if (rtl && rtl(&vi) == 0) {
    char v[64];
    DWORD b = vi.dwBuildNumber ? vi.dwBuildNumber : build;
    snprintf(v, sizeof(v), "%lu.%lu.%lu", vi.dwMajorVersion, vi.dwMinorVersion, b);
    o["version"] = v;
    o["build"] = (int)b;
  } else if (!buildStr.empty()) {
    o["build"] = _wtoi(buildStr.c_str());
  }
  return o;
}

json cpuInfo() {
  json c;
  c["name"] = toUtf8(regStr(HKEY_LOCAL_MACHINE,
                            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                            L"ProcessorNameString"));
  SYSTEM_INFO si{}; GetNativeSystemInfo(&si);
  c["logical"] = (int)si.dwNumberOfProcessors;
  const char* arch = "x86";
  if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) arch = "x64";
  else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) arch = "arm64";
  c["arch"] = arch;
  return c;
}

json memInfo() {
  MEMORYSTATUSEX m{}; m.dwLength = sizeof(m); GlobalMemoryStatusEx(&m);
  json j;
  j["totalMB"] = (double)(m.ullTotalPhys / (1024 * 1024));
  j["availMB"] = (double)(m.ullAvailPhys / (1024 * 1024));
  j["usedPercent"] = (int)m.dwMemoryLoad;
  return j;
}

json disksInfo() {
  json arr = json::array();
  DWORD mask = GetLogicalDrives();
  for (int i = 0; i < 26; ++i) {
    if (!(mask & (1u << i))) continue;
    wchar_t root[4] = {wchar_t(L'A' + i), L':', L'\\', 0};
    UINT t = GetDriveTypeW(root);
    if (t == DRIVE_NO_ROOT_DIR || t == DRIVE_CDROM) continue;
    ULARGE_INTEGER total{}, freeB{};
    json d;
    char letter[3] = {char('A' + i), ':', 0};
    d["drive"] = letter;
    d["type"] = driveTypeStr(t);
    if (GetDiskFreeSpaceExW(root, nullptr, &total, &freeB)) {
      d["totalGB"] = (double)total.QuadPart / (1024.0 * 1024 * 1024);
      d["freeGB"] = (double)freeB.QuadPart / (1024.0 * 1024 * 1024);
    }
    arr.push_back(d);
  }
  return arr;
}

json networkInfo() {
  json arr = json::array();
  ULONG sz = 15000;
  std::vector<BYTE> buf(sz);
  auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                           nullptr, addrs, &sz) == ERROR_BUFFER_OVERFLOW) {
    buf.resize(sz); addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, addrs, &sz);
  }
  for (auto* a = addrs; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp) continue;
    json n; n["adapter"] = toUtf8(a->FriendlyName ? a->FriendlyName : L"");
    json ips = json::array();
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      char ip[INET_ADDRSTRLEN] = {0};
      auto* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
      inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
      if (ip[0]) ips.push_back(ip);
    }
    if (!ips.empty()) { n["ipv4"] = ips; arr.push_back(n); }
  }
  return arr;
}

std::string domainOrWorkgroup(bool& joined) {
  LPWSTR name = nullptr; NETSETUP_JOIN_STATUS st = NetSetupUnknownStatus;
  std::string out;
  if (NetGetJoinInformation(nullptr, &name, &st) == NERR_Success) {
    joined = (st == NetSetupDomainName);
    if (name) out = toUtf8(name);
  }
  if (name) NetApiBufferFree(name);
  return out;
}

}  // namespace

std::string collectSystemInfoJson() {
  json j;
#ifdef PVNC_VERSION
  j["agentVersion"] = PVNC_VERSION;
#endif
  j["host"] = computerName(ComputerNameDnsHostname);
  bool joined = false;
  j["domain"] = domainOrWorkgroup(joined);
  j["domainJoined"] = joined;
  j["user"] = loggedOnUser();
  j["os"] = osInfo();
  j["cpu"] = cpuInfo();
  j["memory"] = memInfo();
  j["disks"] = disksInfo();
  j["network"] = networkInfo();
  j["uptime"] = uptimeStr();
  json sysb;
  sysb["manufacturer"] = toUtf8(regStr(HKEY_LOCAL_MACHINE,
                                       L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer"));
  sysb["model"] = toUtf8(regStr(HKEY_LOCAL_MACHINE,
                                L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName"));
  j["system"] = sysb;
  return j.dump();
}

}  // namespace pvnc
