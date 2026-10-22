#include "rfb_client.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>

#include "pvnc/rfb.h"

#pragma comment(lib, "ws2_32.lib")

namespace pvnc {

namespace {
void putU16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v & 0xFF)); }
void putU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(uint8_t(v >> 24)); b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v & 0xFF));
}
uint16_t rdU16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
int32_t rdS32(const uint8_t* p) {
  return int32_t((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]);
}
}  // namespace

RfbClient::RfbClient(Options opt)
    : opt_(std::move(opt)),
      ctx_(TlsContext::client(opt_.caPath, opt_.certPath, opt_.keyPath)) {}

RfbClient::~RfbClient() { stop(); }

void RfbClient::stop() {
  running_.store(false);
  if (thread_.joinable()) thread_.join();
  conn_.close();
}

RefuseCode RfbClient::connect() {
  ensureWinsock();
  addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  std::string portStr = std::to_string(opt_.port);
  if (getaddrinfo(opt_.host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res)
    return RefuseCode::BadHandshake;
  SOCKET raw = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  int rc = ::connect(raw, res->ai_addr, (int)res->ai_addrlen);
  freeaddrinfo(res);
  if (rc != 0) { closesocket(raw); return RefuseCode::BadHandshake; }

  try {
    conn_ = ctx_.connect(raw);
  } catch (const TlsError&) {
    return RefuseCode::NotAllowed;  // TLS/cert rejected
  }

  auto hs = clientHandshake(conn_, opt_.creds);
  opt_.creds.wipe();
  if (!hs.ok) return hs.refuse;
  RefuseCode authRes = readAuthResult(conn_);
  if (authRes != RefuseCode::None) return authRes;

  if (!rfbHandshake()) return RefuseCode::BadHandshake;

  running_.store(true);
  thread_ = std::thread([this] { runLoop(); });
  return RefuseCode::None;
}

bool RfbClient::rfbHandshake() {
  char ver[rfb::kVersionLen];
  if (!conn_.readFull(ver, rfb::kVersionLen)) return false;
  if (!conn_.writeFull(rfb::kVersion38, rfb::kVersionLen)) return false;

  uint8_t nTypes = 0;
  if (!conn_.readFull(&nTypes, 1)) return false;
  if (nTypes == 0) return false;
  std::vector<uint8_t> types(nTypes);
  if (!conn_.readFull(types.data(), nTypes)) return false;
  bool haveNone = false;
  for (auto t : types) if (t == rfb::kSecNone) haveNone = true;
  if (!haveNone) return false;
  uint8_t chosen = rfb::kSecNone;
  if (!conn_.writeFull(&chosen, 1)) return false;

  uint8_t sr[4];
  if (!conn_.readFull(sr, 4)) return false;
  if (sr[0] || sr[1] || sr[2] || sr[3]) return false;  // SecurityResult != OK

  uint8_t shared = 1;
  if (!conn_.writeFull(&shared, 1)) return false;

  uint8_t init[4];
  if (!conn_.readFull(init, 4)) return false;
  width_ = rdU16(init);
  height_ = rdU16(init + 2);
  uint8_t pf[16];
  if (!conn_.readFull(pf, 16)) return false;  // server pixel format (we assume BGRA32)
  uint8_t nameLen[4];
  if (!conn_.readFull(nameLen, 4)) return false;
  uint32_t nl = (uint32_t(nameLen[0]) << 24) | (nameLen[1] << 16) | (nameLen[2] << 8) | nameLen[3];
  std::vector<uint8_t> name(nl);
  if (nl && !conn_.readFull(name.data(), nl)) return false;

  {
    std::lock_guard<std::mutex> lk(fbMu_);
    fb_.assign(size_t(width_) * height_ * 4, 0);
  }

  // SetEncodings: Raw only.
  std::vector<uint8_t> se;
  se.push_back(rfb::kSetEncodings);
  se.push_back(0);
  putU16(se, 1);
  putU32(se, uint32_t(rfb::kEncRaw));
  if (!conn_.writeFull(se.data(), se.size())) return false;

  conn_.setRecvTimeout(15);
  return sendFbUpdateRequest(false);
}

bool RfbClient::sendFbUpdateRequest(bool incremental) {
  std::vector<uint8_t> r;
  r.push_back(rfb::kFramebufferUpdateRequest);
  r.push_back(incremental ? 1 : 0);
  putU16(r, 0); putU16(r, 0);
  putU16(r, uint16_t(width_)); putU16(r, uint16_t(height_));
  return conn_.writeFull(r.data(), r.size());
}

bool RfbClient::drainInput() {
  std::vector<uint8_t> batch;
  {
    std::lock_guard<std::mutex> lk(inMu_);
    if (inQueue_.empty()) return true;
    batch.swap(inQueue_);
  }
  return conn_.writeFull(batch.data(), batch.size());
}

bool RfbClient::readAndApplyUpdate() {
  uint8_t hdr[3];
  if (!conn_.readFull(hdr, 3)) return false;  // padding + rect count
  uint16_t rects = rdU16(hdr + 1);
  for (uint16_t i = 0; i < rects; ++i) {
    uint8_t rh[12];
    if (!conn_.readFull(rh, 12)) return false;
    int x = rdU16(rh), y = rdU16(rh + 2), w = rdU16(rh + 4), h = rdU16(rh + 6);
    int32_t enc = rdS32(rh + 8);
    if (enc == rfb::kEncRaw) {
      std::vector<uint8_t> data(size_t(w) * h * 4);
      if (!data.empty() && !conn_.readFull(data.data(), data.size())) return false;
      std::lock_guard<std::mutex> lk(fbMu_);
      for (int r = 0; r < h; ++r) {
        int fy = y + r;
        if (fy < 0 || fy >= height_) continue;
        size_t dst = (size_t(fy) * width_ + x) * 4;
        size_t src = size_t(r) * w * 4;
        int cw = w;
        if (x + cw > width_) cw = width_ - x;
        if (cw > 0) std::memcpy(fb_.data() + dst, data.data() + src, size_t(cw) * 4);
      }
    } else if (enc == rfb::kEncCopyRect) {
      uint8_t sp[4];
      if (!conn_.readFull(sp, 4)) return false;  // we don't request this; skip safely
    } else {
      return false;  // unknown encoding: cannot determine length
    }
  }
  if (onUpdate_) onUpdate_();
  return true;
}

void RfbClient::runLoop() {
  bool outstanding = true;  // we sent a full request at the end of handshake
  while (running_.load()) {
    if (!drainInput()) break;
    uint8_t type = 0;
    auto st = conn_.readFullOrIdle(&type, 1);
    if (st == TlsConn::ReadState::Closed || st == TlsConn::ReadState::Error) break;
    if (st == TlsConn::ReadState::WouldBlock) {
      if (!outstanding) { if (!sendFbUpdateRequest(true)) break; outstanding = true; }
      continue;
    }
    switch (type) {
      case rfb::kFramebufferUpdate:
        if (!readAndApplyUpdate()) { running_.store(false); return; }
        outstanding = false;
        break;
      case rfb::kBell:
        break;
      case rfb::kServerCutText: {
        uint8_t h[7];
        if (!conn_.readFull(h, 7)) { running_.store(false); return; }
        uint32_t len = (uint32_t(h[3]) << 24) | (h[4] << 16) | (h[5] << 8) | h[6];
        std::vector<uint8_t> t(len);
        if (len && !conn_.readFull(t.data(), len)) { running_.store(false); return; }
        break;
      }
      case rfb::kSetColourMapEntries: {
        uint8_t h[5];
        if (!conn_.readFull(h, 5)) { running_.store(false); return; }
        uint16_t n = rdU16(h + 3);
        std::vector<uint8_t> c(size_t(n) * 6);
        if (n && !conn_.readFull(c.data(), c.size())) { running_.store(false); return; }
        break;
      }
      default:
        running_.store(false);
        return;
    }
  }
  running_.store(false);
}

void RfbClient::withFrame(const std::function<void(const uint8_t*, int, int)>& fn) {
  std::lock_guard<std::mutex> lk(fbMu_);
  if (fb_.empty()) { fn(nullptr, 0, 0); return; }
  fn(fb_.data(), width_, height_);
}

void RfbClient::pushPointer(uint8_t buttons, int x, int y) {
  std::lock_guard<std::mutex> lk(inMu_);
  inQueue_.push_back(rfb::kPointerEvent);
  inQueue_.push_back(buttons);
  putU16(inQueue_, uint16_t(x < 0 ? 0 : x));
  putU16(inQueue_, uint16_t(y < 0 ? 0 : y));
}

void RfbClient::pushKey(uint32_t keysym, bool down) {
  std::lock_guard<std::mutex> lk(inMu_);
  inQueue_.push_back(rfb::kKeyEvent);
  inQueue_.push_back(down ? 1 : 0);
  inQueue_.push_back(0); inQueue_.push_back(0);
  putU32(inQueue_, keysym);
}

}  // namespace pvnc
