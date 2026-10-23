#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

namespace pvnc {

// The Certificate Authority role: create/open a password-protected CA and issue
// agent/client certificate bundles or sign CSRs. The CA private key lives only
// in memory while unlocked and is never written unencrypted to disk.
class CaPanel {
 public:
  // Builds the panel's controls as children of `parent`. Returns the panel HWND.
  HWND create(HWND parent);
  HWND hwnd() const { return panel_; }
  void onCommand(int id);

  static void commandThunk(void* owner, int id);

 private:
  void refreshStatus();
  void setEnabledForLoaded();
  bool loadSerial();
  bool saveSerial();
  void doCreateCa();
  void doOpenCa();
  void issueBundle(bool client);
  void doSignCsr();

  HWND panel_ = nullptr;
  HWND status_ = nullptr;
  HWND parent_ = nullptr;

  bool loaded_ = false;
  std::string caCertPem_;
  std::string caKeyPem_;  // plaintext, in-memory only
  std::wstring caDir_;
  std::wstring caCn_;
  uint64_t nextSerial_ = 2;  // serial 1 is the CA itself
};

}  // namespace pvnc
