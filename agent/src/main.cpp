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
#include "paths.h"
#include "rfb_server.h"
#include "service.h"
#include "tray.h"

using namespace pvnc;

static int runAgent(const std::string& configPath, uint16_t portOverride) {
  AgentConfig cfg;
  try {
    cfg = loadConfig(configPath);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "config error (%s): %s\n", configPath.c_str(), e.what());
    return 1;
  }
  if (portOverride != 0) cfg.port = portOverride;
  if (cfg.tlsCert.empty() || cfg.tlsKey.empty() || cfg.tlsCa.empty()) {
    std::fprintf(stderr, "config %s: tls.cert/key/ca are required\n", configPath.c_str());
    return 1;
  }
  auto missing = [](const std::string& p) {
    return GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES;
  };
  if (missing(cfg.tlsCert) || missing(cfg.tlsKey) || missing(cfg.tlsCa)) {
    std::fprintf(stderr,
                 "certificates not found (tls.cert/key/ca in %s).\n"
                 "Run provision-certs.ps1 (as admin) in the install folder to generate them,\n"
                 "or provide your own, then (re)start the PortalVNC service.\n",
                 configPath.c_str());
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
  uint16_t portOverride = 0;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--install") mode = "install";
    else if (a == "--uninstall") mode = "uninstall";
    else if (a == "--service") mode = "service";
    else if (a == "--worker") mode = "worker";
    else if (a == "--config" && i + 1 < argc) configPath = argv[++i];
    else if (a == "--port" && i + 1 < argc) portOverride = (uint16_t)std::atoi(argv[++i]);
  }
  if (configPath.empty()) configPath = exeDirUtf8() + "\\config.json";

  if (mode == "install") return installService();
  if (mode == "uninstall") return uninstallService();
  if (mode == "service") return runService();
  // "worker" and default: run the agent in this session.
  return runAgent(configPath, portOverride);
}
