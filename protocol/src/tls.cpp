#include <cstdio>
#include <cstdlib>
#include "pvnc/tls.h"

#include <ws2tcpip.h>

#include <mutex>

#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>

#pragma comment(lib, "ws2_32.lib")

namespace pvnc {

void ensureWinsock() {
  static std::once_flag once;
  std::call_once(once, [] {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      throw TlsError("WSAStartup failed");
    }
  });
}

namespace {
std::string mbedErr(const std::string& where, int rc) {
  char buf[256];
  mbedtls_strerror(rc, buf, sizeof(buf));
  return where + ": " + buf;
}

// BIO callbacks binding mbedTLS to a raw SOCKET.
int sockSend(void* ctx, const unsigned char* buf, size_t len) {
  SOCKET s = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(ctx));
  int n = ::send(s, reinterpret_cast<const char*>(buf), static_cast<int>(len), 0);
  if (n == SOCKET_ERROR) {
    int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
  }
  return n;
}
int sockRecv(void* ctx, unsigned char* buf, size_t len) {
  SOCKET s = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(ctx));
  int n = ::recv(s, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
  if (n == SOCKET_ERROR) {
    int e = WSAGetLastError();
    // A receive timeout (SO_RCVTIMEO) surfaces as WANT_READ so the caller can
    // treat an idle socket as "no data yet" rather than an error.
    if (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
  }
  return n;
}
}  // namespace

struct TlsContext::Impl {
  mbedtls_ssl_config conf;
  mbedtls_x509_crt ca;
  mbedtls_x509_crt ownCert;
  mbedtls_pk_context ownKey;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_entropy_context entropy;
  bool isServer = false;

  Impl() {
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&ca);
    mbedtls_x509_crt_init(&ownCert);
    mbedtls_pk_init(&ownKey);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_entropy_init(&entropy);
  }
  ~Impl() {
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&ca);
    mbedtls_x509_crt_free(&ownCert);
    mbedtls_pk_free(&ownKey);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
  }
};

TlsContext::TlsContext() : d_(std::make_unique<Impl>()) {}
TlsContext::~TlsContext() = default;

void TlsContext::load(const std::string& caPath, const std::string& certPath,
                      const std::string& keyPath, bool isServer) {
  ensureWinsock();
  d_->isServer = isServer;

  const char* pers = "pvnc-tls";
  int rc = mbedtls_ctr_drbg_seed(&d_->drbg, mbedtls_entropy_func, &d_->entropy,
                                 reinterpret_cast<const unsigned char*>(pers),
                                 std::char_traits<char>::length(pers));
  if (rc != 0) throw TlsError(mbedErr("ctr_drbg_seed", rc));

  rc = mbedtls_x509_crt_parse_file(&d_->ca, caPath.c_str());
  if (rc != 0) throw TlsError(mbedErr("parse CA " + caPath, rc));
  rc = mbedtls_x509_crt_parse_file(&d_->ownCert, certPath.c_str());
  if (rc != 0) throw TlsError(mbedErr("parse cert " + certPath, rc));
  rc = mbedtls_pk_parse_keyfile(&d_->ownKey, keyPath.c_str(), nullptr,
                                mbedtls_ctr_drbg_random, &d_->drbg);
  if (rc != 0) throw TlsError(mbedErr("parse key " + keyPath, rc));

  rc = mbedtls_ssl_config_defaults(
      &d_->conf, isServer ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  if (rc != 0) throw TlsError(mbedErr("config_defaults", rc));

  // Pin TLS 1.2: a full-duplex VNC stream over blocking sockets avoids the
  // TLS 1.3 post-handshake (NewSessionTicket / half-RTT client auth) timing
  // that can stall a blocking peer.
  mbedtls_ssl_conf_min_tls_version(&d_->conf, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_max_tls_version(&d_->conf, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_rng(&d_->conf, mbedtls_ctr_drbg_random, &d_->drbg);
  mbedtls_ssl_conf_ca_chain(&d_->conf, &d_->ca, nullptr);
  // Mutual authentication: both ends verify the peer against our CA.
  mbedtls_ssl_conf_authmode(&d_->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  rc = mbedtls_ssl_conf_own_cert(&d_->conf, &d_->ownCert, &d_->ownKey);
  if (rc != 0) throw TlsError(mbedErr("conf_own_cert", rc));
}

TlsContext TlsContext::server(const std::string& ca, const std::string& cert,
                              const std::string& key) {
  TlsContext t;
  t.load(ca, cert, key, true);
  return t;
}
TlsContext TlsContext::client(const std::string& ca, const std::string& cert,
                              const std::string& key) {
  TlsContext t;
  t.load(ca, cert, key, false);
  return t;
}

TlsConn TlsContext::handshake(SOCKET raw, bool isServer) {
  (void)isServer;
  auto ssl = std::make_unique<mbedtls_ssl_context>();
  mbedtls_ssl_init(ssl.get());
  int rc = mbedtls_ssl_setup(ssl.get(), &d_->conf);
  if (rc != 0) {
    mbedtls_ssl_free(ssl.get());
    throw TlsError(mbedErr("ssl_setup", rc));
  }
  // We authenticate the peer by CA trust (and read its CN ourselves), not by
  // hostname/SNI. mbedtls 3.6 requires explicitly acknowledging that.
  mbedtls_ssl_set_hostname(ssl.get(), nullptr);

  mbedtls_ssl_set_bio(ssl.get(),
                      reinterpret_cast<void*>(static_cast<uintptr_t>(raw)),
                      sockSend, sockRecv, nullptr);

  const bool dbg = std::getenv("PVNC_TLS_DEBUG") != nullptr;
  if (dbg) std::fprintf(stderr, "[tls:%s] handshake start sock=%llu\n", isServer ? "srv" : "cli", (unsigned long long)raw);
  while ((rc = mbedtls_ssl_handshake(ssl.get())) != 0) {
    if (dbg) {
      char eb[160]; mbedtls_strerror(rc, eb, sizeof(eb));
      std::fprintf(stderr, "[tls:%s] handshake rc=-0x%04x %s\n", isServer ? "srv" : "cli", (unsigned)(-rc), eb);
    }
    if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
      mbedtls_ssl_free(ssl.get());
      throw TlsError(mbedErr("handshake", rc));
    }
  }
  if (dbg) std::fprintf(stderr, "[tls:%s] handshake done\n", isServer ? "srv" : "cli");
  // VERIFY_REQUIRED already fails the handshake on an untrusted peer, but be
  // explicit so a misconfiguration cannot silently pass.
  uint32_t flags = mbedtls_ssl_get_verify_result(ssl.get());
  if (flags != 0) {
    mbedtls_ssl_free(ssl.get());
    throw TlsError("peer certificate verification failed");
  }

  TlsConn conn;
  conn.sock_ = raw;
  conn.ssl_ = std::move(ssl);
  conn.owns_ssl_ = true;
  const mbedtls_x509_crt* peer = mbedtls_ssl_get_peer_cert(conn.ssl_.get());
  if (peer) {
    char buf[256] = {0};
    if (mbedtls_x509_dn_gets(buf, sizeof(buf), &peer->subject) > 0) {
      std::string dn(buf);
      auto pos = dn.find("CN=");
      if (pos != std::string::npos) {
        auto end = dn.find(',', pos);
        conn.peer_cn_ = dn.substr(pos + 3, end == std::string::npos ? std::string::npos : end - pos - 3);
      }
    }
  }
  return conn;
}

TlsConn TlsContext::accept(SOCKET raw) { return handshake(raw, true); }
TlsConn TlsContext::connect(SOCKET raw) { return handshake(raw, false); }

// ---- TlsConn ----

TlsConn::TlsConn(TlsConn&& o) noexcept { *this = std::move(o); }
TlsConn& TlsConn::operator=(TlsConn&& o) noexcept {
  if (this != &o) {
    close();
    sock_ = o.sock_;
    ssl_ = std::move(o.ssl_);
    peer_cn_ = std::move(o.peer_cn_);
    owns_ssl_ = o.owns_ssl_;
    o.sock_ = INVALID_SOCKET;
    o.owns_ssl_ = false;
  }
  return *this;
}
TlsConn::~TlsConn() { close(); }

long TlsConn::read(void* buf, size_t len) {
  if (!ssl_) return -1;
  int n = mbedtls_ssl_read(ssl_.get(), static_cast<unsigned char*>(buf), len);
  if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
  if (n < 0) {
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) return read(buf, len);
    return -1;
  }
  return n;
}
long TlsConn::write(const void* buf, size_t len) {
  if (!ssl_) return -1;
  int n = mbedtls_ssl_write(ssl_.get(), static_cast<const unsigned char*>(buf), len);
  if (n < 0) {
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) return write(buf, len);
    return -1;
  }
  return n;
}
bool TlsConn::readFull(void* buf, size_t len) {
  auto* p = static_cast<uint8_t*>(buf);
  size_t got = 0;
  while (got < len) {
    long n = read(p + got, len - got);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}
bool TlsConn::writeFull(const void* buf, size_t len) {
  const auto* p = static_cast<const uint8_t*>(buf);
  size_t sent = 0;
  while (sent < len) {
    long n = write(p + sent, len - sent);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}
void TlsConn::setRecvTimeout(int ms) {
  if (sock_ == INVALID_SOCKET) return;
  DWORD t = static_cast<DWORD>(ms);
  ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
}

TlsConn::ReadState TlsConn::readFullOrIdle(void* buf, size_t len) {
  if (!ssl_) return ReadState::Error;
  auto* p = static_cast<uint8_t*>(buf);
  size_t got = 0;
  while (got < len) {
    int n = mbedtls_ssl_read(ssl_.get(), p + got, len - got);
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
      if (got == 0) return ReadState::WouldBlock;  // idle at a message boundary
      continue;                                    // mid-message: keep waiting
    }
    if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) return ReadState::Closed;
    if (n < 0) return ReadState::Error;
    got += static_cast<size_t>(n);
  }
  return ReadState::Ok;
}

void TlsConn::close() {
  if (ssl_ && owns_ssl_) {
    mbedtls_ssl_close_notify(ssl_.get());
    mbedtls_ssl_free(ssl_.get());
  }
  ssl_.reset();
  owns_ssl_ = false;
  if (sock_ != INVALID_SOCKET) {
    ::closesocket(sock_);
    sock_ = INVALID_SOCKET;
  }
}
std::string TlsConn::peerCommonName() const { return peer_cn_; }

}  // namespace pvnc
