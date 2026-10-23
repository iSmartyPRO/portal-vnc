#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "sysinfo.h"

using namespace pvnc;

TEST_CASE("system info reports the agent version", "[sysinfo]") {
  auto j = nlohmann::json::parse(collectSystemInfoJson());
  REQUIRE(j.contains("agentVersion"));
  CHECK(j["agentVersion"].get<std::string>() == std::string(PVNC_VERSION));
}

TEST_CASE("system info has core fields", "[sysinfo]") {
  auto j = nlohmann::json::parse(collectSystemInfoJson());
  CHECK(j.contains("host"));
  CHECK(j.contains("os"));
  CHECK(j.contains("memory"));
}
