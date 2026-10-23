#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pvnc {

// Agent configuration, loaded from config.json next to the executable.
// Missing keys fall back to the safe defaults below.
struct AgentConfig {
  bool trayIcon = false;                 // background service by default
  std::string listenAddress = "0.0.0.0";
  uint16_t port = 7654;
  std::string tlsCert;                   // path to agent certificate (PEM)
  std::string tlsKey;                    // path to agent private key (PEM)
  std::string tlsCa;                     // path to trusted CA (PEM)
  std::vector<std::string> allowedClients;  // IPs / IPv4 CIDRs; empty => deny all
  // Authorization beyond the built-in Administrators group. A connection is
  // allowed if the logged-on user is an Administrator, OR is a member of any
  // listed group, OR is one of the listed users, OR its token holds any listed SID.
  std::vector<std::wstring> accessSids;     // raw SIDs (SDDL form, e.g. "S-1-5-21-...")
  std::vector<std::wstring> accessUsers;    // user names, e.g. "CORP\\jdoe" or "jdoe"
  std::vector<std::wstring> accessGroups;   // group names, e.g. "CORP\\Helpdesk"
  std::string logFile;                   // local connection log path
};

// Loads config from a JSON file. Returns defaults for any absent key.
// Throws std::runtime_error only if the file exists but is not valid JSON.
AgentConfig loadConfig(const std::string& path);

// Parses JSON config from a string (used by loadConfig and tests).
AgentConfig parseConfig(const std::string& json);

// True if `ip` (dotted IPv4) is permitted by `allowed` (exact IPs or a.b.c.d/n
// IPv4 CIDRs). An empty allow-list denies everyone.
bool isClientAllowed(const std::string& ip, const std::vector<std::string>& allowed);

}  // namespace pvnc
