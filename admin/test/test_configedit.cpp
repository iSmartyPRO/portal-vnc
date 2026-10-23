#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "configedit.h"

using namespace pvnc;

TEST_CASE("editing a field preserves unknown keys", "[configedit]") {
  const char* src = R"({
    "port": 7654,
    "customFutureKey": {"x": 1},
    "access": {"sids": ["S-1-5-32-544"]},
    "tls": {"cert": "a.pem", "experimentalFlag": true}
  })";
  auto doc = ConfigDoc::parse(src);
  doc.setPort(7700);

  auto out = nlohmann::json::parse(doc.dump());
  CHECK(out["port"] == 7700);
  CHECK(out["customFutureKey"]["x"] == 1);           // unknown top-level key kept
  CHECK(out["access"]["sids"][0] == "S-1-5-32-544"); // untouched section kept
  CHECK(out["tls"]["experimentalFlag"] == true);     // sibling tls key kept
}

TEST_CASE("setTls preserves sibling tls keys", "[configedit]") {
  auto doc = ConfigDoc::parse(R"({"tls":{"experimentalFlag":true}})");
  doc.setTls("c.pem", "k.pem", "ca.pem");
  auto out = nlohmann::json::parse(doc.dump());
  CHECK(out["tls"]["cert"] == "c.pem");
  CHECK(out["tls"]["key"] == "k.pem");
  CHECK(out["tls"]["ca"] == "ca.pem");
  CHECK(out["tls"]["experimentalFlag"] == true);
}

TEST_CASE("access setters round-trip and are readable", "[configedit]") {
  ConfigDoc doc;  // empty
  doc.setAccessSids({"S-1-5-32-544"});
  doc.setAccessUsers({"CORP\\jdoe"});
  doc.setAccessGroups({"CORP\\Helpdesk"});
  doc.setAllowedClients({"10.0.0.0/24", "192.168.1.5"});

  auto round = ConfigDoc::parse(doc.dump());
  CHECK(round.accessSids() == std::vector<std::string>{"S-1-5-32-544"});
  CHECK(round.accessUsers() == std::vector<std::string>{"CORP\\jdoe"});
  CHECK(round.accessGroups() == std::vector<std::string>{"CORP\\Helpdesk"});
  CHECK(round.allowedClients() ==
        std::vector<std::string>{"10.0.0.0/24", "192.168.1.5"});
}

TEST_CASE("defaults for an empty document", "[configedit]") {
  ConfigDoc doc;
  CHECK(doc.port() == 7654);
  CHECK(doc.listenAddress() == "0.0.0.0");
  CHECK_FALSE(doc.trayIcon());
  CHECK(doc.accessSids().empty());
}

TEST_CASE("malformed JSON throws", "[configedit]") {
  CHECK_THROWS(ConfigDoc::parse("{not json"));
}
