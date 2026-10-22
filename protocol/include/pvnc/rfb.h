#pragma once
#include <cstdint>

// Minimal RFB (VNC) 3.8 wire constants shared by the agent (server) and the
// viewer (client). We implement only what the vertical slice needs: security
// type None (authentication already happened over our mTLS handshake), Raw and
// zlib pixel encodings, and the core client->server input messages.
//
// The stream is standard RFB, so a browser noVNC client (behind the portal
// gateway) can speak it too.

namespace pvnc::rfb {

// 12-byte ProtocolVersion string.
inline constexpr char kVersion38[] = "RFB 003.008\n";
inline constexpr int kVersionLen = 12;

// Security types.
inline constexpr uint8_t kSecNone = 1;

// SecurityResult.
inline constexpr uint32_t kSecResultOk = 0;
inline constexpr uint32_t kSecResultFailed = 1;

// Server -> client message types.
enum ServerMsg : uint8_t {
  kFramebufferUpdate = 0,
  kSetColourMapEntries = 1,
  kBell = 2,
  kServerCutText = 3,
};

// Client -> server message types.
enum ClientMsg : uint8_t {
  kSetPixelFormat = 0,
  kSetEncodings = 2,
  kFramebufferUpdateRequest = 3,
  kKeyEvent = 4,
  kPointerEvent = 5,
  kClientCutText = 6,
};

// Encodings.
enum Encoding : int32_t {
  kEncRaw = 0,
  kEncCopyRect = 1,
  kEncZlib = 6,
  kEncDesktopSize = -223,  // pseudo-encoding
};

// PIXEL_FORMAT (16 bytes). We serve 32bpp little-endian BGRA to match GDI
// capture: bitsPerPixel=32, depth=24, bigEndian=0, trueColour=1,
// red/green/blue max=255, shifts R=16 G=8 B=0.
#pragma pack(push, 1)
struct PixelFormat {
  uint8_t bitsPerPixel;
  uint8_t depth;
  uint8_t bigEndianFlag;
  uint8_t trueColourFlag;
  uint16_t redMax;
  uint16_t greenMax;
  uint16_t blueMax;
  uint8_t redShift;
  uint8_t greenShift;
  uint8_t blueShift;
  uint8_t padding[3];
};
#pragma pack(pop)
static_assert(sizeof(PixelFormat) == 16, "PIXEL_FORMAT must be 16 bytes");

inline PixelFormat defaultPixelFormat() {
  PixelFormat p{};
  p.bitsPerPixel = 32;
  p.depth = 24;
  p.bigEndianFlag = 0;
  p.trueColourFlag = 1;
  p.redMax = 255;
  p.greenMax = 255;
  p.blueMax = 255;
  p.redShift = 16;
  p.greenShift = 8;
  p.blueShift = 0;
  return p;
}

}  // namespace pvnc::rfb
