#include "pvnc/handshake.h"

#include <windows.h>

#include <array>

namespace pvnc {

void Credentials::wipe() {
  if (!password.empty()) {
    SecureZeroMemory(&password[0], password.size() * sizeof(wchar_t));
    password.clear();
  }
}

namespace {

std::string toUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring fromUtf8(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

// A length-prefixed UTF-8 field: uint16 length + bytes.
bool writeField(TlsConn& c, const std::wstring& w) {
  std::string u = toUtf8(w);
  if (u.size() > 0xFFFF) return false;
  uint8_t len[2] = {uint8_t(u.size() >> 8), uint8_t(u.size() & 0xFF)};
  if (!c.writeFull(len, 2)) return false;
  return u.empty() ? true : c.writeFull(u.data(), u.size());
}
bool readField(TlsConn& c, std::wstring& out) {
  uint8_t len[2];
  if (!c.readFull(len, 2)) return false;
  size_t n = (size_t(len[0]) << 8) | len[1];
  std::string u(n, '\0');
  if (n && !c.readFull(u.data(), n)) return false;
  out = fromUtf8(u);
  return true;
}

}  // namespace

HandshakeResult serverHandshake(TlsConn& conn) {
  HandshakeResult r;
  std::array<uint8_t, kClientHelloSize> buf{};
  if (!conn.readFull(buf.data(), buf.size())) {
    r.refuse = RefuseCode::BadHandshake;
    return r;
  }
  auto hello = decodeClientHello(buf.data(), buf.size());
  if (!hello) {
    r.refuse = RefuseCode::BadHandshake;
    ServerHello sh;
    sh.refuse = RefuseCode::BadHandshake;
    auto out = encode(sh);
    conn.writeFull(out.data(), out.size());
    return r;
  }
  auto agreed = negotiateVersion(hello->version, kProtocolVersion);
  ServerHello sh;
  if (!agreed) {
    sh.refuse = RefuseCode::VersionMismatch;
    auto out = encode(sh);
    conn.writeFull(out.data(), out.size());
    r.refuse = RefuseCode::VersionMismatch;
    return r;
  }
  sh.refuse = RefuseCode::None;
  auto out = encode(sh);
  if (!conn.writeFull(out.data(), out.size())) {
    r.refuse = RefuseCode::BadHandshake;
    return r;
  }
  // Read credentials.
  if (!readField(conn, r.creds.domain) || !readField(conn, r.creds.user) ||
      !readField(conn, r.creds.password)) {
    r.refuse = RefuseCode::BadHandshake;
    return r;
  }
  r.ok = true;
  r.agreedVersion = *agreed;
  return r;
}

HandshakeResult clientHandshake(TlsConn& conn, const Credentials& creds) {
  HandshakeResult r;
  ClientHello ch;
  auto out = encode(ch);
  if (!conn.writeFull(out.data(), out.size())) {
    r.refuse = RefuseCode::BadHandshake;
    return r;
  }
  std::array<uint8_t, kServerHelloSize> buf{};
  if (!conn.readFull(buf.data(), buf.size())) {
    r.refuse = RefuseCode::BadHandshake;
    return r;
  }
  auto sh = decodeServerHello(buf.data(), buf.size());
  if (!sh) {
    r.refuse = RefuseCode::BadHandshake;
    return r;
  }
  if (sh->refuse != RefuseCode::None) {
    r.refuse = sh->refuse;
    return r;
  }
  auto agreed = negotiateVersion(kProtocolVersion, sh->version);
  if (!agreed) {
    r.refuse = RefuseCode::VersionMismatch;
    return r;
  }
  if (!writeField(conn, creds.domain) || !writeField(conn, creds.user) ||
      !writeField(conn, creds.password)) {
    r.refuse = RefuseCode::BadHandshake;
    return r;
  }
  r.ok = true;
  r.agreedVersion = *agreed;
  return r;
}

bool sendAuthResult(TlsConn& conn, RefuseCode refuse) {
  uint8_t b[2] = {uint8_t(uint16_t(refuse) >> 8), uint8_t(uint16_t(refuse) & 0xFF)};
  return conn.writeFull(b, 2);
}
RefuseCode readAuthResult(TlsConn& conn) {
  uint8_t b[2];
  if (!conn.readFull(b, 2)) return RefuseCode::BadHandshake;
  return static_cast<RefuseCode>((uint16_t(b[0]) << 8) | b[1]);
}

bool sendSessionMode(TlsConn& conn, SessionMode mode) {
  uint8_t b = static_cast<uint8_t>(mode);
  return conn.writeFull(&b, 1);
}
std::optional<SessionMode> readSessionMode(TlsConn& conn) {
  uint8_t b = 0;
  if (!conn.readFull(&b, 1)) return std::nullopt;
  if (b > static_cast<uint8_t>(SessionMode::Control)) return std::nullopt;
  return static_cast<SessionMode>(b);
}

}  // namespace pvnc
