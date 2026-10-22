#include <catch2/catch_test_macros.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "pvnc/rfb.h"
#include "pvnc/tls.h"
#include "rfb_server.h"

using namespace pvnc;

namespace {
std::string certsDir() {
  const char* d = std::getenv("PVNC_TEST_CERTS");
  return d ? std::string(d) : std::string();
}
std::string cp(const char* n) { return certsDir() + "\\" + n; }

struct Listener {
  SOCKET s = INVALID_SOCKET;
  unsigned short port = 0;
  Listener() {
    ensureWinsock();
    s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    ::bind(s, (sockaddr*)&a, sizeof(a));
    ::listen(s, 1);
    int len = sizeof(a); getsockname(s, (sockaddr*)&a, &len); port = ntohs(a.sin_port);
  }
  ~Listener() { if (s != INVALID_SOCKET) closesocket(s); }
};
SOCKET dial(unsigned short port) {
  ensureWinsock();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(port);
  if (::connect(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); return INVALID_SOCKET; }
  return s;
}
uint16_t rdU16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
int32_t rdS32(const uint8_t* p) {
  return int32_t((uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}
void putU16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v & 0xFF)); }
void putU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(uint8_t(v >> 24)); b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v & 0xFF));
}
}  // namespace

TEST_CASE("RFB server streams a real captured frame over mTLS to a client", "[integration]") {
  if (certsDir().empty()) { WARN("PVNC_TEST_CERTS not set; skipping integration test"); return; }

  Listener lis;
  AgentConfig cfg;
  RfbServer server(cfg);
  server.startForTest();

  std::thread srv([&] {
    auto ctx = TlsContext::server(cp("devca.crt"), cp("agent.crt"), cp("agent.key"));
    SOCKET raw = ::accept(lis.s, nullptr, nullptr);
    if (raw == INVALID_SOCKET) return;
    try {
      auto conn = ctx.accept(raw);
      server.serveClient(conn);  // no Catch2 macros off the main thread
    } catch (...) {}
  });

  auto ctx = TlsContext::client(cp("devca.crt"), cp("client.crt"), cp("client.key"));
  SOCKET raw = dial(lis.port);
  REQUIRE(raw != INVALID_SOCKET);
  auto conn = ctx.connect(raw);

  // --- RFB 3.8 client handshake ---
  char ver[12];
  REQUIRE(conn.readFull(ver, 12));
  REQUIRE(conn.writeFull(rfb::kVersion38, 12));
  uint8_t nTypes = 0;
  REQUIRE(conn.readFull(&nTypes, 1));
  REQUIRE(nTypes >= 1);
  std::vector<uint8_t> types(nTypes);
  REQUIRE(conn.readFull(types.data(), nTypes));
  uint8_t none = rfb::kSecNone;
  REQUIRE(conn.writeFull(&none, 1));
  uint8_t sr[4];
  REQUIRE(conn.readFull(sr, 4));
  CHECK((sr[0] == 0 && sr[1] == 0 && sr[2] == 0 && sr[3] == 0));
  uint8_t shared = 1;
  REQUIRE(conn.writeFull(&shared, 1));

  uint8_t init[4];
  REQUIRE(conn.readFull(init, 4));
  int w = rdU16(init), h = rdU16(init + 2);
  CHECK(w == GetSystemMetrics(SM_CXSCREEN));
  CHECK(h == GetSystemMetrics(SM_CYSCREEN));
  uint8_t pf[16];
  REQUIRE(conn.readFull(pf, 16));
  CHECK(pf[0] == 32);  // bitsPerPixel
  uint8_t nlb[4];
  REQUIRE(conn.readFull(nlb, 4));
  uint32_t nl = (uint32_t(nlb[0]) << 24) | (nlb[1] << 16) | (nlb[2] << 8) | nlb[3];
  std::vector<uint8_t> name(nl);
  if (nl) REQUIRE(conn.readFull(name.data(), nl));

  // Request a full update and verify we receive a Raw rectangle of real pixels.
  std::vector<uint8_t> se{rfb::kSetEncodings, 0}; putU16(se, 1); putU32(se, 0);
  REQUIRE(conn.writeFull(se.data(), se.size()));
  std::vector<uint8_t> req{rfb::kFramebufferUpdateRequest, 0};
  putU16(req, 0); putU16(req, 0); putU16(req, uint16_t(w)); putU16(req, uint16_t(h));
  REQUIRE(conn.writeFull(req.data(), req.size()));

  uint8_t msgType = 0xFF;
  REQUIRE(conn.readFull(&msgType, 1));
  CHECK(msgType == rfb::kFramebufferUpdate);
  uint8_t uhdr[3];
  REQUIRE(conn.readFull(uhdr, 3));
  uint16_t rects = rdU16(uhdr + 1);
  REQUIRE(rects >= 1);
  uint8_t rh[12];
  REQUIRE(conn.readFull(rh, 12));
  int rw = rdU16(rh + 4), rhh = rdU16(rh + 6);
  int32_t enc = rdS32(rh + 8);
  CHECK(enc == rfb::kEncRaw);
  CHECK(rw > 0);
  CHECK(rhh > 0);
  std::vector<uint8_t> pixels(size_t(rw) * rhh * 4);
  REQUIRE(conn.readFull(pixels.data(), pixels.size()));
  bool anyNonZero = false;
  for (size_t i = 0; i < pixels.size(); i += 1021) if (pixels[i]) { anyNonZero = true; break; }
  CHECK(anyNonZero);

  server.stop();
  conn.close();
  srv.join();
}
