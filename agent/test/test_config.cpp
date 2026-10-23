#include <catch2/catch_test_macros.hpp>

#include "config.h"

using namespace pvnc;

TEST_CASE("defaults when keys absent", "[config]") {
  auto c = parseConfig("{}");
  CHECK(c.port == 7654);
  CHECK_FALSE(c.trayIcon);
  CHECK(c.listenAddress == "0.0.0.0");
  CHECK(c.allowedClients.empty());
}

TEST_CASE("parses full config", "[config]") {
  auto c = parseConfig(R"({
    "trayIcon": true,
    "port": 5900,
    "tls": {"cert":"a.crt","key":"a.key","ca":"ca.crt"},
    "allowedClients": ["192.168.1.0/24"],
    "access": {"sids": ["S-1-5-21-1-2-3-1000"], "users": ["CORP\\jdoe"], "groups": ["Helpdesk", "CORP\\Admins"]},
    "logFile": "x.log"
  })");
  CHECK(c.trayIcon);
  CHECK(c.port == 5900);
  CHECK(c.tlsCert == "a.crt");
  CHECK(c.allowedClients.size() == 1);
  CHECK(c.accessSids.size() == 1);
  CHECK(c.accessUsers.size() == 1);
  CHECK(c.accessUsers[0] == L"CORP\\jdoe");
  CHECK(c.accessGroups.size() == 2);
  CHECK(c.logFile == "x.log");
}

TEST_CASE("malformed json throws", "[config]") {
  CHECK_THROWS(parseConfig("{ this is not json "));
}

TEST_CASE("empty allowlist denies all", "[config]") {
  CHECK_FALSE(isClientAllowed("10.0.0.5", {}));
}

TEST_CASE("exact ip allowed", "[config]") {
  CHECK(isClientAllowed("127.0.0.1", {"127.0.0.1"}));
  CHECK_FALSE(isClientAllowed("127.0.0.2", {"127.0.0.1"}));
}

TEST_CASE("cidr match", "[config]") {
  std::vector<std::string> allow{"192.168.1.0/24"};
  CHECK(isClientAllowed("192.168.1.50", allow));
  CHECK_FALSE(isClientAllowed("192.168.2.50", allow));
}

TEST_CASE("cidr /8 and exact mix", "[config]") {
  std::vector<std::string> allow{"10.0.0.0/8", "203.0.113.7"};
  CHECK(isClientAllowed("10.255.1.1", allow));
  CHECK(isClientAllowed("203.0.113.7", allow));
  CHECK_FALSE(isClientAllowed("11.0.0.1", allow));
}
