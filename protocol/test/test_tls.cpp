#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include "pvnc/tls.h"
#include "test_support.h"

using namespace pvnc;
using namespace pvnctest;

TEST_CASE("mutual TLS handshake tunnels bytes and exposes peer CN", "[tls]") {
  if (!haveCerts()) {
    WARN("PVNC_TEST_CERTS not set; skipping mTLS integration test");
    return;
  }
  Listener lis;
  std::atomic<bool> serverCnOk{false};
  std::string serverGot;

  std::thread srv([&] {
    auto ctx = TlsContext::server(certPath("devca.crt"), certPath("agent.crt"),
                                  certPath("agent.key"));
    SOCKET raw = lis.accept();
    if (raw == INVALID_SOCKET) return;  // no Catch2 macros off the main thread
    try {
      auto conn = ctx.accept(raw);
      serverCnOk = (conn.peerCommonName() == "portalvnc-client");
      char buf[16] = {0};
      conn.readFull(buf, 4);
      serverGot = std::string(buf, 4);
      conn.writeFull(buf, 4);  // echo
    } catch (...) {
      closesocket(raw);
    }
  });

  auto cctx = TlsContext::client(certPath("devca.crt"), certPath("client.crt"),
                                 certPath("client.key"));
  SOCKET raw = dialLoopback(lis.port);
  REQUIRE(raw != INVALID_SOCKET);
  auto conn = cctx.connect(raw);
  REQUIRE(conn.writeFull("ping", 4));
  char back[5] = {0};
  REQUIRE(conn.readFull(back, 4));
  srv.join();

  CHECK(std::string(back, 4) == "ping");
  CHECK(serverGot == "ping");
  CHECK(serverCnOk.load());
}

TEST_CASE("client without a CA-signed cert is rejected", "[tls]") {
  if (!haveCerts()) {
    WARN("PVNC_TEST_CERTS not set; skipping");
    return;
  }
  Listener lis;
  std::atomic<bool> serverAccepted{false};

  std::thread srv([&] {
    auto ctx = TlsContext::server(certPath("devca.crt"), certPath("agent.crt"),
                                  certPath("agent.key"));
    SOCKET raw = lis.accept();
    if (raw == INVALID_SOCKET) return;
    try {
      auto conn = ctx.accept(raw);
      serverAccepted = true;  // must NOT happen
      (void)conn;
    } catch (...) {
      closesocket(raw);
    }
  });

  // "rogue" uses a self-signed cert the agent's CA does not trust.
  bool clientThrew = false;
  try {
    auto cctx = TlsContext::client(certPath("devca.crt"), certPath("rogue.crt"),
                                   certPath("rogue.key"));
    SOCKET raw = dialLoopback(lis.port);
    REQUIRE(raw != INVALID_SOCKET);
    auto conn = cctx.connect(raw);
    char x[1];
    conn.readFull(x, 1);
  } catch (const TlsError&) {
    clientThrew = true;
  }
  srv.join();

  CHECK_FALSE(serverAccepted.load());
  CHECK(clientThrew);
}
