#include <catch2/catch_test_macros.hpp>

#include "keyvault.h"

using namespace pvnc;

static const std::string kPem =
    "-----BEGIN PRIVATE KEY-----\nMIIBVgIBADANBgkq...secret...\n-----END PRIVATE KEY-----\n";

TEST_CASE("seal/open roundtrip", "[keyvault]") {
  auto blob = sealKey(kPem, "correct horse");
  REQUIRE(blob.size() > kPem.size());

  std::string out;
  REQUIRE(openKey(blob, "correct horse", out));
  CHECK(out == kPem);
}

TEST_CASE("wrong password fails cleanly", "[keyvault]") {
  auto blob = sealKey(kPem, "correct horse");
  std::string out = "sentinel";
  CHECK_FALSE(openKey(blob, "battery staple", out));
  CHECK(out.empty());
}

TEST_CASE("tampered ciphertext is rejected", "[keyvault]") {
  auto blob = sealKey(kPem, "pw");
  blob.back() ^= 0x01;  // flip a ciphertext bit
  std::string out;
  CHECK_FALSE(openKey(blob, "pw", out));
}

TEST_CASE("truncated/garbage blob returns false", "[keyvault]") {
  std::string out;
  CHECK_FALSE(openKey({1, 2, 3}, "pw", out));
  CHECK_FALSE(openKey(std::vector<uint8_t>(60, 0), "pw", out));  // right size, bad magic
}
