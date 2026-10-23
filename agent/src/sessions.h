#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace pvnc {

// One active operator connection, as reported in status-<port>.json.
struct SessionInfo {
  std::wstring user;   // authenticated Windows user (DOMAIN\\user or user)
  std::string ip;      // peer address
  std::string mode;    // "screen" or "control"
  uint64_t sinceUnix = 0;
};

// Thread-safe registry of active sessions for one worker process.
class SessionRegistry {
 public:
  uint64_t add(const SessionInfo& s);
  void remove(uint64_t id);
  std::vector<SessionInfo> list();

 private:
  std::mutex mu_;
  std::map<uint64_t, SessionInfo> items_;
  uint64_t next_ = 1;
};

// Seconds since the Unix epoch.
uint64_t nowUnix();

// Builds the status JSON for one worker: {agentVersion, startedAt, port, sessions[]}.
std::string sessionsStatusJson(SessionRegistry& reg, uint16_t port, uint64_t startedAtUnix);

}  // namespace pvnc
