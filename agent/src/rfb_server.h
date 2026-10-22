#pragma once
#include <atomic>

#include "capture_hub.h"
#include "config.h"
#include "pvnc/tls.h"

namespace pvnc {

// Bounding box of the pixels that differ between two same-size frames.
struct DirtyRect {
  int x = 0, y = 0, w = 0, h = 0;
  bool empty = true;
};
// Returns the dirty bounding box, or {empty=true} if frames are identical.
// If sizes differ, returns the whole `cur` frame as dirty.
DirtyRect computeDirtyRect(const Frame& prev, const Frame& cur);

// Accepts PortalVNC connections, enforces the IP allow-list, performs the mTLS
// handshake, the protocol handshake and Windows/SID authentication, then serves
// the desktop over RFB. Multiple clients share one CaptureHub.
class RfbServer {
 public:
  explicit RfbServer(const AgentConfig& cfg);
  ~RfbServer();

  // Blocks accepting connections until stop() is called. Returns 0 on clean
  // shutdown, non-zero on fatal startup error (e.g. cannot bind / load certs).
  int run();
  void stop();

  // Serves one already-authenticated client over the given TLS connection.
  // Exposed for testing with a loopback TLS pair.
  void serveClient(TlsConn& conn);

 private:
  AgentConfig cfg_;
  CaptureHub hub_;
  std::atomic<bool> running_{false};
};

}  // namespace pvnc
