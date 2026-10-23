#include "config.h"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <ws2tcpip.h>

#include "pvnc/tls.h"

namespace pvnc {

namespace {
std::wstring toW(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
}  // namespace

AgentConfig parseConfig(const std::string& jsonText) {
  AgentConfig c;
  if (jsonText.empty()) return c;
  nlohmann::json j = nlohmann::json::parse(jsonText);  // throws on malformed

  if (j.contains("trayIcon")) c.trayIcon = j["trayIcon"].get<bool>();
  if (j.contains("listenAddress")) c.listenAddress = j["listenAddress"].get<std::string>();
  if (j.contains("port")) c.port = j["port"].get<uint16_t>();
  if (j.contains("tls")) {
    const auto& t = j["tls"];
    if (t.contains("cert")) c.tlsCert = t["cert"].get<std::string>();
    if (t.contains("key")) c.tlsKey = t["key"].get<std::string>();
    if (t.contains("ca")) c.tlsCa = t["ca"].get<std::string>();
  }
  if (j.contains("allowedClients"))
    for (const auto& e : j["allowedClients"]) c.allowedClients.push_back(e.get<std::string>());
  if (j.contains("access")) {
    const auto& a = j["access"];
    if (a.contains("sids"))
      for (const auto& e : a["sids"]) c.accessSids.push_back(toW(e.get<std::string>()));
    if (a.contains("users"))
      for (const auto& e : a["users"]) c.accessUsers.push_back(toW(e.get<std::string>()));
    if (a.contains("groups"))
      for (const auto& e : a["groups"]) c.accessGroups.push_back(toW(e.get<std::string>()));
  }
  if (j.contains("logFile")) c.logFile = j["logFile"].get<std::string>();
  return c;
}

AgentConfig loadConfig(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return AgentConfig{};  // absent file => all defaults
  std::ostringstream ss;
  ss << in.rdbuf();
  return parseConfig(ss.str());
}

namespace {
bool parseIpv4(const std::string& s, uint32_t& out) {
  in_addr a{};
  if (inet_pton(AF_INET, s.c_str(), &a) != 1) return false;
  out = ntohl(a.s_addr);
  return true;
}
}  // namespace

bool isClientAllowed(const std::string& ip, const std::vector<std::string>& allowed) {
  if (allowed.empty()) return false;  // empty allow-list denies everyone
  ensureWinsock();  // inet_pton needs Winsock initialised; make this self-sufficient
  uint32_t client = 0;
  if (!parseIpv4(ip, client)) return false;

  for (const auto& rule : allowed) {
    auto slash = rule.find('/');
    if (slash == std::string::npos) {
      uint32_t exact = 0;
      if (parseIpv4(rule, exact) && exact == client) return true;
    } else {
      std::string base = rule.substr(0, slash);
      int bits = 0;
      try {
        bits = std::stoi(rule.substr(slash + 1));
      } catch (...) {
        continue;
      }
      if (bits < 0 || bits > 32) continue;
      uint32_t net = 0;
      if (!parseIpv4(base, net)) continue;
      uint32_t mask = (bits == 0) ? 0u : (0xFFFFFFFFu << (32 - bits));
      if ((client & mask) == (net & mask)) return true;
    }
  }
  return false;
}

}  // namespace pvnc
