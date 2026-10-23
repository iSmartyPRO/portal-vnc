#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pvnc {

// A JSON-tree view of an agent config.json. Setters touch only their own key so
// any unknown/future keys in the file survive a round trip. All string values
// are UTF-8 (the agent decodes access names from UTF-8).
class ConfigDoc {
 public:
  ConfigDoc();
  ~ConfigDoc();
  ConfigDoc(ConfigDoc&&) noexcept;
  ConfigDoc& operator=(ConfigDoc&&) noexcept;

  // Builds a doc from JSON text ("" or "{}" yields an empty object). Throws
  // std::runtime_error on malformed JSON.
  static ConfigDoc parse(const std::string& jsonText);
  // Loads from a file; an absent file yields an empty object.
  static ConfigDoc load(const std::string& path);

  // Serializes to pretty JSON (2-space indent).
  std::string dump() const;
  // Writes dump() to `path`. Returns false if the file cannot be opened.
  bool save(const std::string& path) const;

  void setPort(uint16_t port);
  void setListenAddress(const std::string& addr);
  void setTrayIcon(bool on);
  void setTls(const std::string& cert, const std::string& key, const std::string& ca);
  void setAllowedClients(const std::vector<std::string>& ips);
  void setAccessSids(const std::vector<std::string>& sids);
  void setAccessUsers(const std::vector<std::string>& users);
  void setAccessGroups(const std::vector<std::string>& groups);

  uint16_t port() const;              // default 7654 when absent
  std::string listenAddress() const;  // default "0.0.0.0"
  bool trayIcon() const;              // default false
  std::vector<std::string> allowedClients() const;
  std::vector<std::string> accessSids() const;
  std::vector<std::string> accessUsers() const;
  std::vector<std::string> accessGroups() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace pvnc
