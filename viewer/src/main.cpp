// PortalVNC viewer: connects to an agent over mTLS, authenticates with a
// Windows account, and shows the remote desktop in a window.
#include <windows.h>

#include <cstdio>
#include <iostream>
#include <string>

#include "rfb_client.h"
#include "window.h"

using namespace pvnc;

namespace {
std::wstring toW(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

// Reads a password from the console without echoing it.
std::wstring promptPassword() {
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  GetConsoleMode(hIn, &mode);
  SetConsoleMode(hIn, mode & ~ENABLE_ECHO_INPUT);
  std::wcerr << L"Password: ";
  std::wstring pw;
  std::getline(std::wcin, pw);
  SetConsoleMode(hIn, mode);
  std::wcerr << L"\n";
  return pw;
}

const char* refuseText(RefuseCode c) {
  switch (c) {
    case RefuseCode::VersionMismatch: return "protocol version mismatch";
    case RefuseCode::AuthFailed: return "authentication failed (bad credentials)";
    case RefuseCode::AuthNotAdmin: return "access denied: not a member of Administrators";
    case RefuseCode::NotAllowed: return "rejected (TLS/cert or not allowed)";
    case RefuseCode::BadHandshake: return "handshake failed / cannot connect";
    default: return "unknown";
  }
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  RfbClient::Options opt;
  std::wstring password;
  bool havePw = false;

  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    auto next = [&]() -> std::wstring { return (i + 1 < argc) ? argv[++i] : L""; };
    auto toUtf8 = [](const std::wstring& w) {
      int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
      std::string s(n, '\0');
      WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
      return s;
    };
    if (a == L"--host") opt.host = toUtf8(next());
    else if (a == L"--port") opt.port = (uint16_t)_wtoi(next().c_str());
    else if (a == L"--ca") opt.caPath = toUtf8(next());
    else if (a == L"--cert") opt.certPath = toUtf8(next());
    else if (a == L"--key") opt.keyPath = toUtf8(next());
    else if (a == L"--user") opt.creds.user = next();
    else if (a == L"--domain") opt.creds.domain = next();
    else if (a == L"--password") { password = next(); havePw = true; }
  }

  if (opt.host.empty() || opt.caPath.empty() || opt.certPath.empty() ||
      opt.keyPath.empty() || opt.creds.user.empty()) {
    std::fprintf(stderr,
                 "usage: pvnc-viewer --host H --port 7654 --ca ca.crt --cert c.crt "
                 "--key c.key --user USER [--domain DOM] [--password PW]\n");
    return 2;
  }
  if (!havePw) password = promptPassword();
  opt.creds.password = password;

  RfbClient client(std::move(opt));
  RefuseCode rc = client.connect();
  if (rc != RefuseCode::None) {
    std::fprintf(stderr, "connection refused: %s\n", refuseText(rc));
    return 1;
  }

  ViewerWindow win(client);
  if (!win.create(L"PortalVNC viewer")) {
    std::fprintf(stderr, "failed to create window\n");
    return 1;
  }
  int ret = win.runMessageLoop();
  client.stop();
  return ret;
}
