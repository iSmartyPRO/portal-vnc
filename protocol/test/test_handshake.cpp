#include <catch2/catch_test_macros.hpp>

#include <thread>

#include "pvnc/handshake.h"
#include "pvnc/tls.h"
#include "test_support.h"

using namespace pvnc;
using namespace pvnctest;

namespace {
// Runs a TLS server/client pair and hands each side's TlsConn to a callback.
template <class SrvFn, class CliFn>
void withTlsPair(SrvFn srv, CliFn cli) {
  Listener lis;
  std::thread t([&] {
    auto ctx = TlsContext::server(certPath("devca.crt"), certPath("agent.crt"),
                                  certPath("agent.key"));
    SOCKET raw = lis.accept();
    if (raw == INVALID_SOCKET) return;
    try {
      auto conn = ctx.accept(raw);
      srv(conn);
    } catch (...) {
      closesocket(raw);
    }
  });
  auto ctx = TlsContext::client(certPath("devca.crt"), certPath("client.crt"),
                                certPath("client.key"));
  SOCKET raw = dialLoopback(lis.port);
  REQUIRE(raw != INVALID_SOCKET);
  auto conn = ctx.connect(raw);
  cli(conn);
  t.join();
}
}  // namespace

TEST_CASE("handshake exchanges version and credentials", "[handshake]") {
  if (!haveCerts()) { WARN("PVNC_TEST_CERTS not set; skipping"); return; }
  HandshakeResult serverSide;
  withTlsPair(
      [&](TlsConn& c) { serverSide = serverHandshake(c); },
      [&](TlsConn& c) {
        Credentials creds;
        creds.domain = L"CORP";
        creds.user = L"alice";
        creds.password = L"s3cret";
        auto r = clientHandshake(c, creds);
        CHECK(r.ok);
        CHECK(r.agreedVersion == 1);
      });
  REQUIRE(serverSide.ok);
  CHECK(serverSide.creds.domain == L"CORP");
  CHECK(serverSide.creds.user == L"alice");
  CHECK(serverSide.creds.password == L"s3cret");
}

TEST_CASE("version mismatch yields VERSION_MISMATCH refuse", "[handshake]") {
  if (!haveCerts()) { WARN("PVNC_TEST_CERTS not set; skipping"); return; }
  HandshakeResult serverSide;
  RefuseCode clientSawRefuse = RefuseCode::None;
  withTlsPair(
      [&](TlsConn& c) { serverSide = serverHandshake(c); },
      [&](TlsConn& c) {
        // Send a ClientHello with a bogus version by hand.
        ClientHello ch;
        ch.version = 99;
        auto out = encode(ch);
        c.writeFull(out.data(), out.size());
        uint8_t buf[kServerHelloSize];
        c.readFull(buf, sizeof(buf));
        auto sh = decodeServerHello(buf, sizeof(buf));
        if (sh) clientSawRefuse = sh->refuse;
      });
  CHECK_FALSE(serverSide.ok);
  CHECK(serverSide.refuse == RefuseCode::VersionMismatch);
  CHECK(clientSawRefuse == RefuseCode::VersionMismatch);
}
