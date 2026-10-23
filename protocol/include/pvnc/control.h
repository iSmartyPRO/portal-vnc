#pragma once
#include <cstdint>

// Control channel (SessionMode::Control): a simple request/response protocol
// for administration features that are not part of the RFB screen stream.
//
// Request:  opcode(1) + opcode-specific payload
// Response: status(1) + length(4, big-endian) + payload(length bytes)
//   status 0 = ok; non-zero = error (payload is a UTF-8 message).
//
// Used by both the native viewer and the portal gateway.

namespace pvnc::ctl {

enum Opcode : uint8_t {
  kSysInfo = 1,    // payload: none                -> JSON system information
  kFileList = 2,   // payload: u16 path-len + UTF-8 path -> JSON directory listing
  kFileGet = 3,    // payload: u16 path-len + UTF-8 path -> raw file bytes
  kFilePut = 4,    // payload: u16 path-len + path + u64 size + bytes -> JSON {ok}
};

enum Status : uint8_t {
  kOk = 0,
  kError = 1,
  kNotFound = 2,
  kDenied = 3,
};

}  // namespace pvnc::ctl
