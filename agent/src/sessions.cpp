#include "sessions.h"

#include <windows.h>

#include <ctime>

#include <nlohmann/json.hpp>

namespace pvnc {

uint64_t SessionRegistry::add(const SessionInfo& s) {
  std::lock_guard<std::mutex> lk(mu_);
  uint64_t id = next_++;
  items_[id] = s;
  return id;
}
void SessionRegistry::remove(uint64_t id) {
  std::lock_guard<std::mutex> lk(mu_);
  items_.erase(id);
}
std::vector<SessionInfo> SessionRegistry::list() {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<SessionInfo> out;
  out.reserve(items_.size());
  for (auto& kv : items_) out.push_back(kv.second);
  return out;
}

uint64_t nowUnix() { return (uint64_t)std::time(nullptr); }

std::string sessionsStatusJson(SessionRegistry& reg, uint16_t port, uint64_t startedAtUnix) {
  nlohmann::json j;
#ifdef PVNC_VERSION
  j["agentVersion"] = PVNC_VERSION;
#else
  j["agentVersion"] = "";
#endif
  j["startedAt"] = startedAtUnix;
  j["port"] = port;
  auto arr = nlohmann::json::array();
  for (const auto& s : reg.list()) {
    nlohmann::json e;
    int n = WideCharToMultiByte(CP_UTF8, 0, s.user.data(), (int)s.user.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.user.data(), (int)s.user.size(), u.data(), n, nullptr, nullptr);
    e["user"] = u;
    e["ip"] = s.ip;
    e["mode"] = s.mode;
    e["since"] = s.sinceUnix;
    arr.push_back(e);
  }
  j["sessions"] = arr;
  return j.dump();
}

}  // namespace pvnc
