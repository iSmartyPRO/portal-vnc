#include "ui_ca.h"

#include <string>
#include <vector>

#include "ca.h"
#include "fileio.h"
#include "keyvault.h"
#include "ui_common.h"

namespace pvnc {

namespace {
enum : int {
  IDC_CA_CREATE = 3001,
  IDC_CA_OPEN,
  IDC_CA_AGENT,
  IDC_CA_CLIENT,
  IDC_CA_CSR,
};
const wchar_t* kCertPem = L"ca-cert.pem";
const wchar_t* kKeyVault = L"ca-key.vault";
const wchar_t* kSerial = L"ca-serial.txt";

std::wstring computerName() {
  wchar_t buf[256];
  DWORD n = 256;
  if (GetComputerNameW(buf, &n)) return std::wstring(buf, n);
  return L"agent-01";
}
}  // namespace

void CaPanel::commandThunk(void* owner, int id) {
  static_cast<CaPanel*>(owner)->onCommand(id);
}

HWND CaPanel::create(HWND parent) {
  parent_ = parent;
  panel_ = createPanelWindow(parent, this, &CaPanel::commandThunk);

  makeGroup(panel_, L"Certificate Authority", 16, 12, 720, 150);
  status_ = CreateWindowExW(0, L"STATIC", L"No CA loaded.", WS_CHILD | WS_VISIBLE, 32, 40, 688, 70,
                            panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
  makeButton(panel_, IDC_CA_CREATE, L"Create CA…", 32, 120, 150, 30);
  makeButton(panel_, IDC_CA_OPEN, L"Open CA…", 196, 120, 150, 30);

  makeGroup(panel_, L"Issue certificates", 16, 180, 720, 120);
  makeButton(panel_, IDC_CA_AGENT, L"Issue agent bundle…", 32, 214, 210, 30);
  makeButton(panel_, IDC_CA_CLIENT, L"Issue client bundle…", 256, 214, 210, 30);
  makeButton(panel_, IDC_CA_CSR, L"Sign CSR…", 480, 214, 150, 30);

  applyGuiFont(panel_);
  setEnabledForLoaded();
  return panel_;
}

void CaPanel::setEnabledForLoaded() {
  for (int id : {IDC_CA_AGENT, IDC_CA_CLIENT, IDC_CA_CSR})
    EnableWindow(GetDlgItem(panel_, id), loaded_);
}

void CaPanel::refreshStatus() {
  std::wstring s;
  if (loaded_) {
    s = L"CA: " + caCn_ + L"\nFolder: " + caDir_ + L"\nNext serial: " + std::to_wstring(nextSerial_) +
        L"\nThe CA key is unlocked in memory (never written unencrypted).";
  } else {
    s = L"No CA loaded. Create a new CA or open an existing one.";
  }
  SetWindowTextW(status_, s.c_str());
  setEnabledForLoaded();
}

bool CaPanel::loadSerial() {
  std::string t;
  if (readTextFile(joinPath(caDir_, kSerial), t)) {
    try {
      nextSerial_ = std::stoull(t);
    } catch (...) {
      nextSerial_ = 2;
    }
  } else {
    nextSerial_ = 2;
  }
  return true;
}
bool CaPanel::saveSerial() {
  return writeTextFile(joinPath(caDir_, kSerial), std::to_string(nextSerial_));
}

void CaPanel::doCreateCa() {
  std::wstring dir;
  if (!pickFolder(parent_, L"Choose a folder to store the CA", dir)) return;
  std::vector<Field> f{{L"Common name", L"PortalVNC CA", false},
                       {L"Password", L"", true},
                       {L"Confirm password", L"", true}};
  if (!promptFields(parent_, L"Create CA", f)) return;
  if (f[1].value.empty()) { warn(parent_, L"Password must not be empty."); return; }
  if (f[1].value != f[2].value) { warn(parent_, L"Passwords do not match."); return; }

  try {
    KeyPair ca = generateRsaKey(4096);
    std::string cert = makeSelfSignedCa(ca.privPem, wToUtf8(f[0].value), 3650);
    auto blob = sealKey(ca.privPem, wToUtf8(f[1].value));
    if (!writeTextFile(joinPath(dir, kCertPem), cert) ||
        !writeBinFile(joinPath(dir, kKeyVault), blob)) {
      warn(parent_, L"Failed to write the CA files.");
      return;
    }
    caDir_ = dir;
    caCertPem_ = cert;
    caKeyPem_ = ca.privPem;
    caCn_ = f[0].value;
    nextSerial_ = 2;
    saveSerial();
    loaded_ = true;
    refreshStatus();
    info(parent_, L"CA created in\n" + dir);
  } catch (const std::exception& e) {
    warn(parent_, L"CA creation failed: " + utf8ToW(e.what()));
  }
}

void CaPanel::doOpenCa() {
  std::wstring dir;
  if (!pickFolder(parent_, L"Choose the CA folder (ca-cert.pem + ca-key.vault)", dir)) return;
  std::string cert;
  std::vector<uint8_t> blob;
  if (!readTextFile(joinPath(dir, kCertPem), cert) ||
      !readBinFile(joinPath(dir, kKeyVault), blob)) {
    warn(parent_, L"ca-cert.pem or ca-key.vault not found in that folder.");
    return;
  }
  std::vector<Field> f{{L"Password", L"", true}};
  if (!promptFields(parent_, L"Open CA", f)) return;
  std::string key;
  if (!openKey(blob, wToUtf8(f[0].value), key)) {
    warn(parent_, L"Wrong password or the key container is corrupt.");
    return;
  }
  caDir_ = dir;
  caCertPem_ = cert;
  caKeyPem_ = key;
  caCn_ = L"(loaded)";
  loadSerial();
  loaded_ = true;
  refreshStatus();
  info(parent_, L"CA unlocked.");
}

void CaPanel::issueBundle(bool client) {
  if (!loaded_) return;
  std::wstring defCn = client ? L"client-01" : computerName();
  std::vector<Field> f{{L"Common name", defCn, false}};
  if (!promptFields(parent_, client ? L"Issue client bundle" : L"Issue agent bundle", f)) return;
  std::wstring dest;
  if (!pickFolder(parent_, L"Choose a folder for the bundle", dest)) return;
  try {
    KeyPair leaf = generateRsaKey(2048);
    std::string cert =
        issueCert(caCertPem_, caKeyPem_, leaf.pubPem, wToUtf8(f[0].value), 825, nextSerial_);
    ++nextSerial_;
    saveSerial();
    const wchar_t* certName = client ? L"client-cert.pem" : L"agent-cert.pem";
    const wchar_t* keyName = client ? L"client-key.pem" : L"agent-key.pem";
    bool ok = writeTextFile(joinPath(dest, L"ca.pem"), caCertPem_) &&
              writeTextFile(joinPath(dest, certName), cert) &&
              writeTextFile(joinPath(dest, keyName), leaf.privPem);
    if (!ok) { warn(parent_, L"Failed to write the bundle files."); return; }
    info(parent_, std::wstring(L"Bundle written to\n") + dest +
                      L"\n\nca.pem, " + certName + L", " + keyName);
  } catch (const std::exception& e) {
    warn(parent_, L"Issue failed: " + utf8ToW(e.what()));
  }
}

void CaPanel::doSignCsr() {
  if (!loaded_) return;
  std::wstring csrPath;
  if (!openFileDialog(parent_, L"Choose a CSR to sign",
                      L"CSR / PEM\0*.csr;*.pem;*.req\0All files\0*.*\0", csrPath))
    return;
  std::string csr;
  if (!readTextFile(csrPath, csr)) { warn(parent_, L"Cannot read the CSR file."); return; }
  std::wstring out;
  if (!saveFileDialog(parent_, L"Save the signed certificate", L"PEM\0*.pem\0", L"signed-cert.pem",
                      out))
    return;
  try {
    std::string cert = signCsr(caCertPem_, caKeyPem_, csr, 825, nextSerial_);
    ++nextSerial_;
    saveSerial();
    if (!writeTextFile(out, cert)) { warn(parent_, L"Failed to write the certificate."); return; }
    info(parent_, L"Certificate written to\n" + out);
  } catch (const std::exception& e) {
    warn(parent_, L"Signing failed: " + utf8ToW(e.what()));
  }
}

void CaPanel::onCommand(int id) {
  switch (id) {
    case IDC_CA_CREATE: doCreateCa(); break;
    case IDC_CA_OPEN: doOpenCa(); break;
    case IDC_CA_AGENT: issueBundle(false); break;
    case IDC_CA_CLIENT: issueBundle(true); break;
    case IDC_CA_CSR: doSignCsr(); break;
  }
}

}  // namespace pvnc
