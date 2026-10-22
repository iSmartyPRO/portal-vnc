#pragma once
#include <winsock2.h>
#include <memory>
#include <stdexcept>
#include <string>

#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>

namespace pvnc {

class TlsError : public std::runtime_error {
 public:
  explicit TlsError(const std::string& what) : std::runtime_error(what) {}
};

// Ensures Winsock is initialised for the lifetime of the process.
void ensureWinsock();

class TlsConn {
 public:
  TlsConn() = default;
  TlsConn(TlsConn&&) noexcept;
  TlsConn& operator=(TlsConn&&) noexcept;
  TlsConn(const TlsConn&) = delete;
  TlsConn& operator=(const TlsConn&) = delete;
  ~TlsConn();

  // Blocking read/write of the decrypted stream. Return >0 bytes, 0 on clean
  // close, <0 on error.
  long read(void* buf, size_t len);
  long write(const void* buf, size_t len);
  // Read exactly len bytes (false on short/closed). Convenience for framing.
  bool readFull(void* buf, size_t len);
  bool writeFull(const void* buf, size_t len);
  void close();

  // Sets a receive timeout so an idle readFullOrIdle() can return WouldBlock
  // instead of blocking forever (lets a single thread both read input and push
  // frame updates).
  void setRecvTimeout(int ms);

  enum class ReadState { Ok, WouldBlock, Closed, Error };
  // Reads exactly len bytes. Returns WouldBlock only when no bytes have arrived
  // yet (message boundary is idle); once a message starts it blocks for the
  // rest. Requires setRecvTimeout to have been set for WouldBlock to occur.
  ReadState readFullOrIdle(void* buf, size_t len);

  // Common name from the peer certificate subject (empty if none).
  std::string peerCommonName() const;

  SOCKET socket() const { return sock_; }

 private:
  friend class TlsContext;
  SOCKET sock_ = INVALID_SOCKET;
  std::unique_ptr<mbedtls_ssl_context> ssl_;
  std::string peer_cn_;
  bool owns_ssl_ = false;
};

// Holds the certificate material and configuration for one side (server/client).
// Both sides require mutual authentication (VERIFY_REQUIRED).
class TlsContext {
 public:
  static TlsContext server(const std::string& caPath, const std::string& certPath,
                           const std::string& keyPath);
  static TlsContext client(const std::string& caPath, const std::string& certPath,
                           const std::string& keyPath);

  // Defined out-of-line (in tls.cpp) so the pimpl `Impl` need not be complete
  // where a TlsContext is moved or stored in a smart pointer.
  TlsContext(TlsContext&&) noexcept;
  TlsContext& operator=(TlsContext&&) noexcept;
  ~TlsContext();

  // Perform the TLS handshake over an already-connected raw socket.
  TlsConn accept(SOCKET raw);   // server side
  TlsConn connect(SOCKET raw);  // client side

 private:
  TlsContext();
  void load(const std::string& caPath, const std::string& certPath,
            const std::string& keyPath, bool isServer);
  TlsConn handshake(SOCKET raw, bool isServer);

  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace pvnc
