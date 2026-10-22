// PortalVNC agent entry point.
//
// Modes:
//   --install      register the Windows service (run as admin)
//   --uninstall    remove the Windows service
//   --service      run under the SCM (used by the service binary path)
//   --worker       run the agent in the current session (launched by the service)
//   --config PATH  run as a console agent for development (default)
//
// The service is a thin supervisor that launches "--worker" as SYSTEM in the
// active session; the worker does capture/input/RFB and follows the active
// desktop (lock screen / UAC).
#include <windows.h>

#include <cstdio>
#include <string>

#include "config.h"
#include "rfb_server.h"
#include "service.h"
#include "tray.h"

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

static int runAgent(const std::string& configPath) {
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

  TrayIcon tray;
  if (cfg.trayIcon) tray.start();

  std::printf("PortalVNC agent listening on %s:%u (tray=%s)\n",
              cfg.listenAddress.c_str(), cfg.port, cfg.trayIcon ? "on" : "off");
  RfbServer server(cfg);
  int rc;
  try {
    rc = server.run();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    rc = 5;
  }
  tray.stop();
  return rc;
}

int main(int argc, char** argv) {
  std::string configPath;
  std::string mode;  // install|uninstall|service|worker|console
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--install") mode = "install";
    else if (a == "--uninstall") mode = "uninstall";
    else if (a == "--service") mode = "service";
    else if (a == "--worker") mode = "worker";
    else if (a == "--config" && i + 1 < argc) configPath = argv[++i];
  }
  if (configPath.empty()) configPath = exeDir() + "\\config.json";

  if (mode == "install") return installService();
  if (mode == "uninstall") return uninstallService();
  if (mode == "service") return runService();
  // "worker" and default: run the agent in this session.
  return runAgent(configPath);
}
