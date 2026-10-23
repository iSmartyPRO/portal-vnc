#include "control_server.h"

#include <cstdint>
#include <string>
#include <vector>

#include "pvnc/control.h"
#include "sysinfo.h"

namespace pvnc {

namespace {

bool sendResponse(TlsConn& c, uint8_t status, const std::string& payload) {
  std::vector<uint8_t> out;
  out.reserve(5 + payload.size());
  out.push_back(status);
  uint32_t n = static_cast<uint32_t>(payload.size());
  out.push_back(uint8_t(n >> 24)); out.push_back(uint8_t(n >> 16));
  out.push_back(uint8_t(n >> 8)); out.push_back(uint8_t(n));
  out.insert(out.end(), payload.begin(), payload.end());
  return c.writeFull(out.data(), out.size());
}

}  // namespace

void serveControl(TlsConn& conn) {
  for (;;) {
    uint8_t op = 0;
    auto st = conn.readFullOrIdle(&op, 1);
    if (st == TlsConn::ReadState::Closed || st == TlsConn::ReadState::Error) break;
    if (st == TlsConn::ReadState::WouldBlock) continue;

    switch (op) {
      case ctl::kSysInfo: {
        std::string j = collectSystemInfoJson();
        if (!sendResponse(conn, ctl::kOk, j)) return;
        break;
      }
      // File operations (kFileList/kFileGet/kFilePut) are added in a later step.
      default:
        sendResponse(conn, ctl::kError, "unsupported opcode");
        return;
    }
  }
}

}  // namespace pvnc
