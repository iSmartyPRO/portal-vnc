// PortalVNC agent: loads config.json, serves the desktop over RFB-in-mTLS.
// This build runs as a console process; the Windows service wrapper and the
// secure-desktop helper are later plans.
#include <windows.h>

#include <cstdio>
#include <string>

#include "config.h"
#include "rfb_server.h"

using namespace pvnc;

static std::string exeDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring w(buf, n);
  auto slash = w.find_last_of(L"\\/");
  std::wstring dir = (slash == std::wstring::npos) ? L"." : w.substr(0, slash);
  int len = WideCharToMultiByte(CP_UTF8, 0, dir.data(), (int)dir.size(), nullptr, 0, nullptr, nullptr);
  std::string s(len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, dir.data(), (int)dir.size(), s.data(), len, nullptr, nullptr);
  return s;
}

int main(int argc, char** argv) {
  std::string configPath;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--config" && i + 1 < argc) configPath = argv[++i];
  }
  if (configPath.empty()) configPath = exeDir() + "\\config.json";

  AgentConfig cfg;
  try {
    cfg = loadConfig(configPath);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "config error (%s): %s\n", configPath.c_str(), e.what());
    return 1;
  }

  if (cfg.tlsCert.empty() || cfg.tlsKey.empty() || cfg.tlsCa.empty()) {
    std::fprintf(stderr, "config %s: tls.cert/key/ca are required\n", configPath.c_str());
    return 1;
  }

  std::printf("PortalVNC agent listening on %s:%u (tray=%s)\n",
              cfg.listenAddress.c_str(), cfg.port, cfg.trayIcon ? "on" : "off");
  RfbServer server(cfg);
  try {
    return server.run();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 5;
  }
}
