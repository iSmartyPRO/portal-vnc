#include <catch2/catch_test_macros.hpp>
#include "pvnc/protocol.h"

using namespace pvnc;

TEST_CASE("encode/decode ClientHello roundtrip") {
  ClientHello in;
  in.capabilities = 0;
  auto bytes = encode(in);
  REQUIRE(bytes.size() == kClientHelloSize);
  auto out = decodeClientHello(bytes.data(), bytes.size());
  REQUIRE(out.has_value());
  CHECK(out->magic == kMagic);
  CHECK(out->version == kProtocolVersion);
  CHECK(out->capabilities == 0u);
}

TEST_CASE("encode/decode ServerHello roundtrip with refuse") {
  ServerHello in;
  in.refuse = RefuseCode::VersionMismatch;
  auto bytes = encode(in);
  REQUIRE(bytes.size() == kServerHelloSize);
  auto out = decodeServerHello(bytes.data(), bytes.size());
  REQUIRE(out.has_value());
  CHECK(out->refuse == RefuseCode::VersionMismatch);
}

TEST_CASE("decode rejects wrong magic") {
  ClientHello in;
  auto bytes = encode(in);
  bytes[0] ^= 0xFF;  // corrupt magic
  CHECK_FALSE(decodeClientHello(bytes.data(), bytes.size()).has_value());
}

TEST_CASE("decode rejects short buffer") {
  ClientHello in;
  auto bytes = encode(in);
  CHECK_FALSE(decodeClientHello(bytes.data(), bytes.size() - 1).has_value());
}

TEST_CASE("negotiateVersion equal ok") {
  auto v = negotiateVersion(1, 1);
  REQUIRE(v.has_value());
  CHECK(*v == 1);
}

TEST_CASE("negotiateVersion mismatch") {
  CHECK_FALSE(negotiateVersion(1, 2).has_value());
}
