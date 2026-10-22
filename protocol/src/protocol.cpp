#include "pvnc/protocol.h"

namespace pvnc {
namespace {

void putU16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v & 0xFF));
}
void putU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  b.push_back(static_cast<uint8_t>(v & 0xFF));
}
uint16_t getU16(const uint8_t* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
uint32_t getU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

}  // namespace

std::vector<uint8_t> encode(const ClientHello& h) {
  std::vector<uint8_t> b;
  b.reserve(kClientHelloSize);
  putU32(b, h.magic);
  putU16(b, h.version);
  putU32(b, h.capabilities);
  return b;
}

std::vector<uint8_t> encode(const ServerHello& h) {
  std::vector<uint8_t> b;
  b.reserve(kServerHelloSize);
  putU32(b, h.magic);
  putU16(b, h.version);
  putU32(b, h.capabilities);
  putU16(b, static_cast<uint16_t>(h.refuse));
  return b;
}

std::optional<ClientHello> decodeClientHello(const uint8_t* data, size_t len) {
  if (len < kClientHelloSize) return std::nullopt;
  ClientHello h;
  h.magic = getU32(data);
  h.version = getU16(data + 4);
  h.capabilities = getU32(data + 6);
  if (h.magic != kMagic) return std::nullopt;
  return h;
}

std::optional<ServerHello> decodeServerHello(const uint8_t* data, size_t len) {
  if (len < kServerHelloSize) return std::nullopt;
  ServerHello h;
  h.magic = getU32(data);
  h.version = getU16(data + 4);
  h.capabilities = getU32(data + 6);
  h.refuse = static_cast<RefuseCode>(getU16(data + 10));
  if (h.magic != kMagic) return std::nullopt;
  return h;
}

std::optional<uint16_t> negotiateVersion(uint16_t client, uint16_t server) {
  if (client != server) return std::nullopt;
  return client;
}

}  // namespace pvnc
