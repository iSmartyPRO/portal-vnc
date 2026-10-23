#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "pvnc/handshake.h"
#include "pvnc/tls.h"

namespace pvnc {

// Connects to a PortalVNC agent over mTLS, authenticates, then runs the RFB
// client protocol on a background thread: it decodes framebuffer updates into a
// shared BGRA buffer and sends queued input events. All TLS I/O stays on that
// one thread; the UI thread only enqueues input and reads the framebuffer under
// the provided lock.
class RfbClient {
 public:
  struct Options {
    std::string host;
    uint16_t port = 7654;
    std::string caPath, certPath, keyPath;
    Credentials creds;
  };

  explicit RfbClient(Options opt);
  ~RfbClient();

  // Connects + authenticates (blocking). Returns None on success, or the
  // RefuseCode explaining rejection. On success the receive loop is running.
  RefuseCode connect();
  void stop();

  // Called (from the receive thread) after the framebuffer changes.
  void onFrameUpdated(std::function<void()> cb) { onUpdate_ = std::move(cb); }

  int width() const { return width_; }
  int height() const { return height_; }

  // Access the framebuffer for painting. The callback runs under the fb lock.
  void withFrame(const std::function<void(const uint8_t* bgra, int w, int h)>& fn);

  // Enqueue input (from the UI thread).
  void pushPointer(uint8_t buttonMask, int x, int y);
  void pushKey(uint32_t keysym, bool down);

  // Opens a short-lived control connection and returns the agent's system
  // information as a JSON string. Blocking; safe to call from the UI thread.
  bool fetchSystemInfo(std::string& jsonOut);

  // File transfer over a control connection (blocking; UI-thread safe).
  // path empty on fileList lists the drives.
  bool fileList(const std::wstring& path, std::string& jsonOut);
  bool fileDownload(const std::wstring& remotePath, const std::wstring& localPath);
  bool fileUpload(const std::wstring& localPath, const std::wstring& remotePath);

  // Request a full (non-incremental) framebuffer update on the next loop.
  void requestRefresh() { fullRefresh_.store(true); }

 private:
  void runLoop();
  // Dials, does the mTLS + PortalVNC handshake + auth, and sends the session
  // mode. Returns None on success (out is ready for RFB or control traffic).
  RefuseCode dialAndAuth(TlsConn& out, SessionMode mode);
  bool rfbHandshake();            // RFB 3.8 init after our auth
  bool sendFbUpdateRequest(bool incremental);
  bool drainInput();              // send queued input events
  bool readAndApplyUpdate();      // one FramebufferUpdate message

  Options opt_;
  TlsContext ctx_;
  TlsConn conn_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> fullRefresh_{false};

  int width_ = 0, height_ = 0;
  std::mutex fbMu_;
  std::vector<uint8_t> fb_;       // BGRA, width_*height_*4

  std::mutex inMu_;
  std::vector<uint8_t> inQueue_;  // pre-encoded RFB client messages

  std::function<void()> onUpdate_;
};

}  // namespace pvnc
