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

// ---- control channel: file transfer round-trip ----
#include <windows.h>
#include "control_server.h"
#include "pvnc/control.h"

namespace {
void wU16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(uint8_t(v>>8)); b.push_back(uint8_t(v&0xFF)); }
void wU64(std::vector<uint8_t>& b, uint64_t v){ for(int i=7;i>=0;--i) b.push_back(uint8_t(v>>(i*8))); }
}

TEST_CASE("control channel uploads, lists and downloads a file over mTLS", "[integration]") {
  if (certsDir().empty()) { WARN("PVNC_TEST_CERTS not set; skipping"); return; }

  // A temp path to round-trip through the agent file handlers.
  wchar_t tmpDir[MAX_PATH]; GetTempPathW(MAX_PATH, tmpDir);
  std::wstring path = std::wstring(tmpDir) + L"pvnc_ctl_test.bin";
  DeleteFileW(path.c_str());
  auto toU8 = [](const std::wstring& w){
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);
    std::string s(n,'\0'); WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr); return s; };
  std::string pathU8 = toU8(path);
  std::string content = "PortalVNC control-channel file transfer test payload \x01\x02\x03";

  Listener lis;
  std::thread srv([&]{
    auto ctx = pvnc::TlsContext::server(cp("devca.crt"), cp("agent.crt"), cp("agent.key"));
    SOCKET raw = ::accept(lis.s, nullptr, nullptr);
    if (raw == INVALID_SOCKET) return;
    try { auto c = ctx.accept(raw); pvnc::serveControl(c); } catch(...) {}
  });

  auto ctx = pvnc::TlsContext::client(cp("devca.crt"), cp("client.crt"), cp("client.key"));
  SOCKET raw = dial(lis.port);
  REQUIRE(raw != INVALID_SOCKET);
  auto conn = ctx.connect(raw);

  // PUT
  {
    std::vector<uint8_t> req; req.push_back(pvnc::ctl::kFilePut);
    wU16(req, (uint16_t)pathU8.size()); req.insert(req.end(), pathU8.begin(), pathU8.end());
    wU64(req, content.size()); req.insert(req.end(), content.begin(), content.end());
    REQUIRE(conn.writeFull(req.data(), req.size()));
    uint8_t h[5]; REQUIRE(conn.readFull(h,5)); CHECK(h[0]==pvnc::ctl::kOk);
    uint32_t len=(uint32_t(h[1])<<24)|(h[2]<<16)|(h[3]<<8)|h[4];
    std::vector<uint8_t> body(len); if(len) conn.readFull(body.data(),len);
  }
  // LIST the temp dir -> our file should be present
  {
    std::string dir = toU8(std::wstring(tmpDir));
    std::vector<uint8_t> req; req.push_back(pvnc::ctl::kFileList);
    wU16(req,(uint16_t)dir.size()); req.insert(req.end(), dir.begin(), dir.end());
    REQUIRE(conn.writeFull(req.data(), req.size()));
    uint8_t h[5]; REQUIRE(conn.readFull(h,5)); CHECK(h[0]==pvnc::ctl::kOk);
    uint32_t len=(uint32_t(h[1])<<24)|(h[2]<<16)|(h[3]<<8)|h[4];
    std::string js(len,'\0'); if(len) REQUIRE(conn.readFull(js.data(),len));
    CHECK(js.find("pvnc_ctl_test.bin") != std::string::npos);
  }
  // GET -> bytes match
  {
    std::vector<uint8_t> req; req.push_back(pvnc::ctl::kFileGet);
    wU16(req,(uint16_t)pathU8.size()); req.insert(req.end(), pathU8.begin(), pathU8.end());
    REQUIRE(conn.writeFull(req.data(), req.size()));
    uint8_t status=0; REQUIRE(conn.readFull(&status,1)); REQUIRE(status==pvnc::ctl::kOk);
    uint8_t szb[8]; REQUIRE(conn.readFull(szb,8));
    uint64_t sz=0; for(int i=0;i<8;++i) sz=(sz<<8)|szb[i];
    CHECK(sz==content.size());
    std::string got(sz,'\0'); if(sz) REQUIRE(conn.readFull(got.data(),sz));
    CHECK(got==content);
  }
  conn.close(); srv.join();
  DeleteFileW(path.c_str());
}
