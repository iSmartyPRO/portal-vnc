#include "rfb_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

#include "pvnc/auth.h"
#include "pvnc/handshake.h"
#include "pvnc/rfb.h"

#pragma comment(lib, "ws2_32.lib")

namespace pvnc {

DirtyRect computeDirtyRect(const Frame& prev, const Frame& cur) {
  DirtyRect d;
  if (cur.width <= 0 || cur.height <= 0) return d;
  if (prev.width != cur.width || prev.height != cur.height ||
      prev.bgra.size() != cur.bgra.size() || prev.bgra.empty()) {
    d.x = 0; d.y = 0; d.w = cur.width; d.h = cur.height; d.empty = false;
    return d;
  }
  int minX = cur.width, minY = cur.height, maxX = -1, maxY = -1;
  const int stride = cur.stride;
  for (int y = 0; y < cur.height; ++y) {
    const uint8_t* a = prev.bgra.data() + static_cast<size_t>(y) * stride;
    const uint8_t* b = cur.bgra.data() + static_cast<size_t>(y) * stride;
    if (std::memcmp(a, b, stride) == 0) continue;
    minY = std::min(minY, y);
    maxY = std::max(maxY, y);
    // Narrow the x-extent within this changed row (per 4-byte pixel).
    int lo = 0, hi = cur.width - 1;
    while (lo < cur.width && std::memcmp(a + lo * 4, b + lo * 4, 4) == 0) ++lo;
    while (hi >= lo && std::memcmp(a + hi * 4, b + hi * 4, 4) == 0) --hi;
    minX = std::min(minX, lo);
    maxX = std::max(maxX, hi);
  }
  if (maxY < 0) return d;  // identical
  d.x = minX; d.y = minY; d.w = maxX - minX + 1; d.h = maxY - minY + 1;
  d.empty = false;
  return d;
}

namespace {

void putU16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v & 0xFF));
}
void putU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(uint8_t(v >> 24)); b.push_back(uint8_t(v >> 16));
  b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v & 0xFF));
}
uint16_t rdU16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
uint32_t rdU32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Serialises a FramebufferUpdate with one Raw rectangle cut from `f`.
std::vector<uint8_t> rawUpdate(const Frame& f, const DirtyRect& r) {
  std::vector<uint8_t> out;
  out.push_back(rfb::kFramebufferUpdate);
  out.push_back(0);        // padding
  putU16(out, 1);          // one rectangle
  putU16(out, uint16_t(r.x));
  putU16(out, uint16_t(r.y));
  putU16(out, uint16_t(r.w));
  putU16(out, uint16_t(r.h));
  putU32(out, uint32_t(rfb::kEncRaw));
  out.reserve(out.size() + size_t(r.w) * r.h * 4);
  for (int y = 0; y < r.h; ++y) {
    const uint8_t* row = f.bgra.data() + size_t(r.y + y) * f.stride + size_t(r.x) * 4;
    out.insert(out.end(), row, row + size_t(r.w) * 4);
  }
  return out;
}

}  // namespace

RfbServer::RfbServer(const AgentConfig& cfg) : cfg_(cfg) {}
RfbServer::~RfbServer() { stop(); }

void RfbServer::stop() { running_.store(false); }

void RfbServer::startForTest() {
  running_.store(true);
  hub_.start();
}

void RfbServer::serveClient(TlsConn& conn) {
  // --- RFB 3.8 handshake (security None; we already authenticated) ---
  if (!conn.writeFull(rfb::kVersion38, rfb::kVersionLen)) return;
  char ver[rfb::kVersionLen];
  if (!conn.readFull(ver, rfb::kVersionLen)) return;

  uint8_t sec[2] = {1, rfb::kSecNone};
  if (!conn.writeFull(sec, 2)) return;
  uint8_t chosen = 0;
  if (!conn.readFull(&chosen, 1)) return;

  uint32_t secResult = 0;  // OK
  uint8_t sr[4] = {0, 0, 0, 0};
  (void)secResult;
  if (!conn.writeFull(sr, 4)) return;

  uint8_t sharedFlag = 0;
  if (!conn.readFull(&sharedFlag, 1)) return;

  // --- ServerInit ---
  Frame frame;
  hub_.latest(frame);
  if (frame.width == 0) return;
  std::vector<uint8_t> init;
  putU16(init, uint16_t(frame.width));
  putU16(init, uint16_t(frame.height));
  rfb::PixelFormat pf = rfb::defaultPixelFormat();
  init.insert(init.end(), reinterpret_cast<uint8_t*>(&pf), reinterpret_cast<uint8_t*>(&pf) + sizeof(pf));
  const char* name = "PortalVNC";
  putU32(init, uint32_t(std::strlen(name)));
  init.insert(init.end(), name, name + std::strlen(name));
  if (!conn.writeFull(init.data(), init.size())) return;

  Frame lastSent;  // empty => next update is full

  conn.setRecvTimeout(20);
  bool pending = false;
  bool pendingFull = false;

  while (running_.load()) {
    uint8_t type = 0;
    auto st = conn.readFullOrIdle(&type, 1);
    if (st == TlsConn::ReadState::Closed || st == TlsConn::ReadState::Error) break;

    if (st == TlsConn::ReadState::Ok) {
      switch (type) {
        case rfb::kSetPixelFormat: {
          uint8_t buf[19];
          if (!conn.readFull(buf, sizeof(buf))) return;  // 3 pad + 16 pf (ignored)
          break;
        }
        case rfb::kSetEncodings: {
          uint8_t hdr[3];
          if (!conn.readFull(hdr, 3)) return;  // 1 pad + u16 count
          uint16_t count = rdU16(hdr + 1);
          std::vector<uint8_t> enc(size_t(count) * 4);
          if (count && !conn.readFull(enc.data(), enc.size())) return;
          break;
        }
        case rfb::kFramebufferUpdateRequest: {
          uint8_t buf[9];
          if (!conn.readFull(buf, sizeof(buf))) return;
          pending = true;
          if (buf[0] == 0) { pendingFull = true; }  // non-incremental
          break;
        }
        case rfb::kKeyEvent: {
          uint8_t buf[7];
          if (!conn.readFull(buf, sizeof(buf))) return;
          bool down = buf[0] != 0;
          uint32_t key = rdU32(buf + 3);
          hub_.enqueueKey(key, down);
          break;
        }
        case rfb::kPointerEvent: {
          uint8_t buf[5];
          if (!conn.readFull(buf, sizeof(buf))) return;
          hub_.enqueuePointer(buf[0], rdU16(buf + 1), rdU16(buf + 3));
          break;
        }
        case rfb::kClientCutText: {
          uint8_t hdr[7];
          if (!conn.readFull(hdr, 7)) return;  // 3 pad + u32 len
          uint32_t len = rdU32(hdr + 3);
          std::vector<uint8_t> txt(len);
          if (len && !conn.readFull(txt.data(), len)) return;
          break;
        }
        default:
          return;  // unknown message: drop the client
      }
    }

    // Send an update if one was requested and there is something to show.
    if (pending) {
      Frame cur;
      hub_.latest(cur);
      DirtyRect r;
      if (pendingFull || lastSent.width != cur.width || lastSent.height != cur.height) {
        r.x = 0; r.y = 0; r.w = cur.width; r.h = cur.height; r.empty = (cur.width == 0);
      } else {
        r = computeDirtyRect(lastSent, cur);
      }
      if (!r.empty) {
        auto msg = rawUpdate(cur, r);
        if (!conn.writeFull(msg.data(), msg.size())) break;
        lastSent = std::move(cur);
        pending = false;
        pendingFull = false;
      }
      // If nothing changed on an incremental request, keep `pending` and wait.
    }
  }
}

int RfbServer::run() {
  ensureWinsock();
  tls_ = std::make_unique<TlsContext>(TlsContext::server(cfg_.tlsCa, cfg_.tlsCert, cfg_.tlsKey));

  SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) return 2;
  BOOL yes = TRUE;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&yes), sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(cfg_.port);
  if (inet_pton(AF_INET, cfg_.listenAddress.c_str(), &addr.sin_addr) != 1)
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(listener);
    return 3;
  }
  if (::listen(listener, SOMAXCONN) != 0) {
    closesocket(listener);
    return 4;
  }

  hub_.start();
  running_.store(true);

  while (running_.load()) {
    sockaddr_in peer{};
    int plen = sizeof(peer);
    SOCKET raw = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &plen);
    if (raw == INVALID_SOCKET) continue;

    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    if (!isClientAllowed(ip, cfg_.allowedClients)) {
      closesocket(raw);  // reject before TLS
      continue;
    }

    std::thread([this, raw] {
      TlsConn conn;
      try {
        conn = tls_->accept(raw);
      } catch (...) {
        closesocket(raw);
        return;  // bad/missing client cert: TLS rejected
      }
      auto hs = serverHandshake(conn);
      if (!hs.ok) {
        sendAuthResult(conn, hs.refuse);
        return;
      }
      AuthOutcome outcome = authenticate(hs.creds, cfg_.accessSids);
      hs.creds.wipe();
      sendAuthResult(conn, toRefuse(outcome));
      if (outcome != AuthOutcome::Ok) return;
      serveClient(conn);
    }).detach();
  }

  closesocket(listener);
  hub_.stop();
  return 0;
}

}  // namespace pvnc
