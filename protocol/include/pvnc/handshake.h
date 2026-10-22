#pragma once
#include <string>

#include "pvnc/protocol.h"
#include "pvnc/tls.h"

namespace pvnc {

struct Credentials {
  std::wstring domain;
  std::wstring user;
  std::wstring password;

  // Overwrite the password buffer in memory.
  void wipe();
};

struct HandshakeResult {
  bool ok = false;
  RefuseCode refuse = RefuseCode::None;
  uint16_t agreedVersion = 0;
  Credentials creds;  // populated on the server side when ok
};

// Server side: read ClientHello, negotiate version, then read credentials.
// On version mismatch sends a ServerHello carrying RefuseCode::VersionMismatch
// and returns ok=false. The caller (auth) decides on the credentials.
HandshakeResult serverHandshake(TlsConn& conn);

// Client side: send ClientHello, read ServerHello. If the server refuses,
// returns that code. Otherwise sends credentials and returns ok=true.
HandshakeResult clientHandshake(TlsConn& conn, const Credentials& creds);

// Server side: after authenticating, tell the client the outcome.
// refuse == None means accepted; otherwise the client sees why.
bool sendAuthResult(TlsConn& conn, RefuseCode refuse);
// Client side: read the auth outcome sent by sendAuthResult.
RefuseCode readAuthResult(TlsConn& conn);

}  // namespace pvnc
