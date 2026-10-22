#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace pvnc {

// Wire magic "PVN1" and the current protocol version.
constexpr uint32_t kMagic = 0x50564E31u;  // 'P','V','N','1'
constexpr uint16_t kProtocolVersion = 1;

// Capability bits (reserved for future negotiation; none required yet).
constexpr uint32_t kCapNone = 0u;

// Structured refusal reasons sent by the server during the handshake.
enum class RefuseCode : uint16_t {
  None = 0,
  VersionMismatch = 1,
  AuthFailed = 2,
  AuthNotAdmin = 3,
  NotAllowed = 4,
  BadHandshake = 5,
};

struct ClientHello {
  uint32_t magic = kMagic;
  uint16_t version = kProtocolVersion;
  uint32_t capabilities = kCapNone;
};

struct ServerHello {
  uint32_t magic = kMagic;
  uint16_t version = kProtocolVersion;
  uint32_t capabilities = kCapNone;
  RefuseCode refuse = RefuseCode::None;  // None unless the server rejects the client.
};

// Fixed wire sizes (big-endian on the wire).
constexpr size_t kClientHelloSize = 4 + 2 + 4;          // magic + version + caps
constexpr size_t kServerHelloSize = 4 + 2 + 4 + 2;      // + refuse

std::vector<uint8_t> encode(const ClientHello& h);
std::vector<uint8_t> encode(const ServerHello& h);

std::optional<ClientHello> decodeClientHello(const uint8_t* data, size_t len);
std::optional<ServerHello> decodeServerHello(const uint8_t* data, size_t len);

// Returns the agreed version (currently requires exact equality) or nullopt.
std::optional<uint16_t> negotiateVersion(uint16_t client, uint16_t server);

}  // namespace pvnc
