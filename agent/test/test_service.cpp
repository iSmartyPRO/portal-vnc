#include <catch2/catch_test_macros.hpp>

#include "service.h"

using namespace pvnc;

TEST_CASE("queryService returns Running for a known service", "[service]") {
  // Dnscache is present and running on every normal Windows install.
  auto s = queryService(L"Dnscache");
  CHECK((s == SvcState::Running || s == SvcState::StartPending));
}

TEST_CASE("queryService returns NotInstalled for a bogus name", "[service]") {
  CHECK(queryService(L"pvnc_no_such_service_zzz") == SvcState::NotInstalled);
}
