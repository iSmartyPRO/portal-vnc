#include "configedit.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace pvnc {

struct ConfigDoc::Impl {
  nlohmann::json j = nlohmann::json::object();
};

ConfigDoc::ConfigDoc() : d_(std::make_unique<Impl>()) {}
ConfigDoc::~ConfigDoc() = default;
ConfigDoc::ConfigDoc(ConfigDoc&&) noexcept = default;
ConfigDoc& ConfigDoc::operator=(ConfigDoc&&) noexcept = default;

ConfigDoc ConfigDoc::parse(const std::string& jsonText) {
  ConfigDoc doc;
  std::string t = jsonText;
  // Treat whitespace-only / empty as an empty object.
  auto nonWs = t.find_first_not_of(" \t\r\n");
  if (nonWs == std::string::npos) return doc;
  nlohmann::json j = nlohmann::json::parse(t);  // throws on malformed
  if (!j.is_object()) throw std::runtime_error("config root is not a JSON object");
  doc.d_->j = std::move(j);
  return doc;
}

ConfigDoc ConfigDoc::load(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return ConfigDoc{};
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse(ss.str());
}

std::string ConfigDoc::dump() const { return d_->j.dump(2); }

bool ConfigDoc::save(const std::string& path) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  std::string s = dump();
  out.write(s.data(), (std::streamsize)s.size());
  return (bool)out;
}

void ConfigDoc::setPort(uint16_t port) { d_->j["port"] = port; }
void ConfigDoc::setListenAddress(const std::string& addr) { d_->j["listenAddress"] = addr; }
void ConfigDoc::setTrayIcon(bool on) { d_->j["trayIcon"] = on; }

void ConfigDoc::setTls(const std::string& cert, const std::string& key, const std::string& ca) {
  auto& t = d_->j["tls"];
  if (!t.is_object()) t = nlohmann::json::object();  // preserve any sibling tls keys
  t["cert"] = cert;
  t["key"] = key;
  t["ca"] = ca;
}

void ConfigDoc::setAllowedClients(const std::vector<std::string>& ips) {
  d_->j["allowedClients"] = ips;
}

namespace {
void setAccess(nlohmann::json& j, const char* key, const std::vector<std::string>& v) {
  auto& a = j["access"];
  if (!a.is_object()) a = nlohmann::json::object();
  a[key] = v;
}
std::vector<std::string> getArr(const nlohmann::json& j, const char* a, const char* b = nullptr) {
  std::vector<std::string> out;
  const nlohmann::json* node = &j;
  if (!node->contains(a)) return out;
  node = &(*node)[a];
  if (b) {
    if (!node->is_object() || !node->contains(b)) return out;
    node = &(*node)[b];
  }
  if (node->is_array())
    for (const auto& e : *node)
      if (e.is_string()) out.push_back(e.get<std::string>());
  return out;
}
}  // namespace

void ConfigDoc::setAccessSids(const std::vector<std::string>& v) { setAccess(d_->j, "sids", v); }
void ConfigDoc::setAccessUsers(const std::vector<std::string>& v) { setAccess(d_->j, "users", v); }
void ConfigDoc::setAccessGroups(const std::vector<std::string>& v) { setAccess(d_->j, "groups", v); }

uint16_t ConfigDoc::port() const {
  return d_->j.value("port", (uint16_t)7654);
}
std::string ConfigDoc::listenAddress() const {
  return d_->j.value("listenAddress", std::string("0.0.0.0"));
}
bool ConfigDoc::trayIcon() const { return d_->j.value("trayIcon", false); }

std::vector<std::string> ConfigDoc::allowedClients() const { return getArr(d_->j, "allowedClients"); }
std::vector<std::string> ConfigDoc::accessSids() const { return getArr(d_->j, "access", "sids"); }
std::vector<std::string> ConfigDoc::accessUsers() const { return getArr(d_->j, "access", "users"); }
std::vector<std::string> ConfigDoc::accessGroups() const { return getArr(d_->j, "access", "groups"); }

}  // namespace pvnc
