#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "sessions.h"

using namespace pvnc;

TEST_CASE("registry add/list/remove", "[sessions]") {
  SessionRegistry reg;
  REQUIRE(reg.list().empty());

  uint64_t a = reg.add({L"CORP\\jdoe", "10.0.0.5", "screen", 1000});
  uint64_t b = reg.add({L"iNB", "127.0.0.1", "control", 1001});
  REQUIRE(a != b);
  REQUIRE(reg.list().size() == 2);

  reg.remove(a);
  auto rest = reg.list();
  REQUIRE(rest.size() == 1);
  REQUIRE(rest[0].user == L"iNB");
  REQUIRE(rest[0].mode == "control");

  reg.remove(b);
  REQUIRE(reg.list().empty());
}

TEST_CASE("status json carries worker metadata and sessions", "[sessions]") {
  SessionRegistry reg;
  reg.add({L"CORP\\jdoe", "10.0.0.5", "screen", 1700000000});

  auto j = nlohmann::json::parse(sessionsStatusJson(reg, 7654, 1699990000));
  REQUIRE(j.contains("agentVersion"));
  REQUIRE(j["port"] == 7654);
  REQUIRE(j["startedAt"] == 1699990000);
  REQUIRE(j["sessions"].is_array());
  REQUIRE(j["sessions"].size() == 1);
  REQUIRE(j["sessions"][0]["user"] == "CORP\\jdoe");
  REQUIRE(j["sessions"][0]["ip"] == "10.0.0.5");
  REQUIRE(j["sessions"][0]["mode"] == "screen");
  REQUIRE(j["sessions"][0]["since"] == 1700000000);
}
