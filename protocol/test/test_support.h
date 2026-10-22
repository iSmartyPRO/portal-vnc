#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

#include "pvnc/tls.h"

namespace pvnctest {

// Directory holding dev certs (devca.crt, agent.crt/key, client.crt/key),
// from the PVNC_TEST_CERTS environment variable.
inline std::string certsDir() {
  const char* d = std::getenv("PVNC_TEST_CERTS");
  return d ? std::string(d) : std::string();
}
inline bool haveCerts() { return !certsDir().empty(); }
inline std::string certPath(const char* name) { return certsDir() + "\\" + name; }

// A listening socket on an ephemeral localhost port.
struct Listener {
  SOCKET s = INVALID_SOCKET;
  unsigned short port = 0;
  Listener() {
    pvnc::ensureWinsock();
    s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) throw std::runtime_error("bind");
    if (::listen(s, 1) != 0) throw std::runtime_error("listen");
    int len = sizeof(addr);
    getsockname(s, (sockaddr*)&addr, &len);
    port = ntohs(addr.sin_port);
  }
  ~Listener() { if (s != INVALID_SOCKET) closesocket(s); }
  SOCKET accept() { return ::accept(s, nullptr, nullptr); }
};

inline SOCKET dialLoopback(unsigned short port) {
  pvnc::ensureWinsock();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
    closesocket(s);
    return INVALID_SOCKET;
  }
  return s;
}

}  // namespace pvnctest
