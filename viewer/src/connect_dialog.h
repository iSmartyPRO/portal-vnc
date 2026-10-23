#pragma once
#include <cstdint>
#include <string>

namespace pvnc {

// Connection details gathered from the GUI dialog (or prefilled from the
// command line). Certificate paths default to the executable's folder.
struct ConnectInfo {
  std::wstring host = L"127.0.0.1";
  uint16_t port = 7654;
  std::wstring user;
  std::wstring password;
  std::string caPath, certPath, keyPath;
};

// Shows a modal connect dialog prefilled from `io`. Returns true if the user
// clicked Connect (fields written back into `io`), false on Cancel/close.
bool showConnectDialog(ConnectInfo& io);

}  // namespace pvnc
