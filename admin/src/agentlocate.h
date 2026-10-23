#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "service.h"   // SvcState (from agent core)
#include "sessions.h"  // SessionInfo (from agent core)

namespace pvnc {

// Where the agent is installed on this machine and its current service state.
struct AgentInstall {
  bool installed = false;        // the PortalVNC service exists
  std::wstring exePath;          // full path to pvnc-agent.exe (from the service ImagePath)
  std::wstring configDir;        // folder holding config.json / status-*.json
  SvcState state = SvcState::NotInstalled;
  std::wstring exeVersion;       // file version of exePath, e.g. "0.1.9" ("" if unknown)
};

// Default install/config location when the service is not registered.
std::wstring defaultConfigDir();  // C:\ProgramData\PortalVNC

// Locates the agent via the PortalVNC service ImagePath; falls back to the
// default config dir. Never throws — reports installed=false when absent.
AgentInstall locateAgent();

// Live status merged across every worker's status-<port>.json in `dir`.
struct LiveStatus {
  std::wstring agentVersion;             // first non-empty agentVersion seen
  std::vector<SessionInfo> sessions;     // all sessions across all workers
  std::vector<uint16_t> ports;           // one entry per status file read, ascending
};

// Reads and merges all status-*.json files in `dir`. Missing dir / unreadable
// or malformed files are skipped; never throws.
LiveStatus aggregateStatus(const std::wstring& dir);

}  // namespace pvnc
