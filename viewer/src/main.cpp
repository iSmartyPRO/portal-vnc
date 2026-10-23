// PortalVNC viewer: connects to an agent over mTLS, authenticates with a
// Windows account, and shows the remote desktop in a window.
//
// GUI application. Launched with no (or partial) connection arguments it shows
// a connect dialog; launched with --password it connects headlessly (for
// scripts / the portal). Certificates default to the executable's folder.
#include <windows.h>
#include <shellapi.h>

#include <string>

#include "connect_dialog.h"
#include "rfb_client.h"
#include "window.h"

using namespace pvnc;

namespace {

std::wstring exeDirW() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring w(buf, n);
  auto slash = w.find_last_of(L"\\/");
  return (slash == std::wstring::npos) ? L"." : w.substr(0, slash);
}

std::string toUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring currentUser() {
  wchar_t name[256]; DWORD n = 256;
  return GetUserNameW(name, &n) ? std::wstring(name, n ? n - 1 : 0) : std::wstring();
}

const wchar_t* refuseText(RefuseCode c) {
  switch (c) {
    case RefuseCode::VersionMismatch: return L"Protocol version mismatch.";
    case RefuseCode::AuthFailed: return L"Authentication failed (wrong user or password).";
    case RefuseCode::AuthNotAdmin: return L"Access denied: the account is not authorized (not an administrator).";
    case RefuseCode::NotAllowed: return L"Rejected: certificate not trusted, or this address is not in the agent's allowedClients.";
    case RefuseCode::BadHandshake: return L"Could not connect (host unreachable, wrong port, or handshake failed).";
    default: return L"Unknown error.";
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
  ConnectInfo info;
  info.host = L"127.0.0.1";
  info.port = 7654;
  info.user = currentUser();
  std::wstring dir = exeDirW();
  info.caPath = toUtf8(dir + L"\\devca.crt");
  info.certPath = toUtf8(dir + L"\\client.crt");
  info.keyPath = toUtf8(dir + L"\\client.key");

  bool havePassword = false;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    auto next = [&]() -> std::wstring { return (i + 1 < argc) ? argv[++i] : L""; };
    if (a == L"--host") info.host = next();
    else if (a == L"--port") info.port = (uint16_t)_wtoi(next().c_str());
    else if (a == L"--ca") info.caPath = toUtf8(next());
    else if (a == L"--cert") info.certPath = toUtf8(next());
    else if (a == L"--key") info.keyPath = toUtf8(next());
    else if (a == L"--user") info.user = next();
    else if (a == L"--password") { info.password = next(); havePassword = true; }
  }
  if (argv) LocalFree(argv);

  // Headless only when a password was supplied on the command line; otherwise
  // show the connect dialog (prefilled).
  if (!havePassword) {
    if (!showConnectDialog(info)) return 0;  // cancelled
  }

  RfbClient::Options opt;
  opt.host = toUtf8(info.host);
  opt.port = info.port;
  opt.caPath = info.caPath;
  opt.certPath = info.certPath;
  opt.keyPath = info.keyPath;
  opt.creds.user = info.user;
  opt.creds.password = info.password;

  RfbClient client(std::move(opt));
  RefuseCode rc = client.connect();
  if (rc != RefuseCode::None) {
    MessageBoxW(nullptr, refuseText(rc), L"PortalVNC — connection failed", MB_ICONERROR);
    return 1;
  }

  ViewerWindow win(client);
  if (!win.create(L"PortalVNC viewer")) {
    MessageBoxW(nullptr, L"Failed to create the viewer window.", L"PortalVNC", MB_ICONERROR);
    return 1;
  }
  int ret = win.runMessageLoop();
  client.stop();
  return ret;
}
