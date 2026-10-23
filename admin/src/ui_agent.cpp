#include "ui_agent.h"

#include <commctrl.h>
#include <ws2tcpip.h>

#include <ctime>
#include <string>

#include "fileio.h"
#include "picker.h"
#include "service.h"
#include "ui_common.h"

#pragma comment(lib, "ws2_32.lib")

namespace pvnc {

namespace {
enum : int {
  IDC_AG_REFRESH = 4001,
  IDC_AG_INSTALL,
  IDC_AG_UNINSTALL,
  IDC_AG_START,
  IDC_AG_STOP,
  IDC_AG_RESTART,
  IDC_AG_IMPORT,
  IDC_AG_ADDIP,
  IDC_AG_DELIP,
  IDC_AG_ADDPRIN,
  IDC_AG_DELPRIN,
  IDC_AG_SAVE,
  IDC_AG_IPS = 4101,
  IDC_AG_ACCESS = 4102,
  IDC_AG_SESSIONS = 4103,
  IDC_AG_PORT = 4201,
  IDC_AG_LISTEN = 4202,
  IDC_AG_TRAY = 4203,
};

void info(HWND h, const std::wstring& m) { MessageBoxW(h, m.c_str(), L"PortalVNC Manager", MB_ICONINFORMATION); }
void warn(HWND h, const std::wstring& m) { MessageBoxW(h, m.c_str(), L"PortalVNC Manager", MB_ICONWARNING); }

const wchar_t* stateName(SvcState s) {
  switch (s) {
    case SvcState::NotInstalled: return L"not installed";
    case SvcState::Stopped: return L"stopped";
    case SvcState::Running: return L"running";
    case SvcState::StartPending: return L"starting";
    case SvcState::StopPending: return L"stopping";
    default: return L"unknown";
  }
}

std::wstring fmtTime(uint64_t unix) {
  if (!unix) return L"";
  std::time_t t = (std::time_t)unix;
  std::tm tmv{};
  localtime_s(&tmv, &t);
  wchar_t b[32];
  wcsftime(b, 32, L"%Y-%m-%d %H:%M:%S", &tmv);
  return b;
}

// Accepts a bare IPv4 or an a.b.c.d/n (0..32) range.
bool validIpv4OrCidr(const std::wstring& w) {
  std::string s = wToUtf8(w);
  auto slash = s.find('/');
  std::string base = (slash == std::string::npos) ? s : s.substr(0, slash);
  in_addr a{};
  if (InetPtonA(AF_INET, base.c_str(), &a) != 1) return false;
  if (slash != std::string::npos) {
    try {
      int bits = std::stoi(s.substr(slash + 1));
      if (bits < 0 || bits > 32) return false;
    } catch (...) {
      return false;
    }
  }
  return true;
}
}  // namespace

void AgentPanel::commandThunk(void* owner, int id) {
  static_cast<AgentPanel*>(owner)->onCommand(id);
}

HWND AgentPanel::create(HWND parent) {
  parent_ = parent;
  panel_ = createPanelWindow(parent, this, &AgentPanel::commandThunk);

  // --- Status group ---
  makeGroup(panel_, L"Status", 16, 8, 724, 150);
  status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 28, 30, 700, 64, panel_,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
  makeButton(panel_, IDC_AG_REFRESH, L"Refresh", 28, 104, 108, 30);
  makeButton(panel_, IDC_AG_INSTALL, L"Install", 144, 104, 108, 30);
  makeButton(panel_, IDC_AG_UNINSTALL, L"Uninstall", 260, 104, 108, 30);
  makeButton(panel_, IDC_AG_START, L"Start", 376, 104, 108, 30);
  makeButton(panel_, IDC_AG_STOP, L"Stop", 492, 104, 108, 30);
  makeButton(panel_, IDC_AG_RESTART, L"Restart", 608, 104, 108, 30);

  // --- Sessions group ---
  makeGroup(panel_, L"Active sessions", 16, 166, 724, 130);
  sessions_ = makeListView(panel_, IDC_AG_SESSIONS, 28, 188, 700, 98);
  lvAddColumn(sessions_, 0, L"User", 240);
  lvAddColumn(sessions_, 1, L"Address", 150);
  lvAddColumn(sessions_, 2, L"Mode", 90);
  lvAddColumn(sessions_, 3, L"Since", 200);

  // --- Certificates group ---
  makeGroup(panel_, L"Certificates", 16, 304, 724, 76);
  certs_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 28, 328, 520, 40, panel_,
                           nullptr, GetModuleHandleW(nullptr), nullptr);
  makeButton(panel_, IDC_AG_IMPORT, L"Import bundle…", 560, 332, 168, 30);

  // --- Access & network group ---
  makeGroup(panel_, L"Access & network", 16, 388, 724, 266);
  makeLabel(panel_, L"Allowed client IPs / CIDRs", 28, 410, 320, 20);
  ips_ = makeListView(panel_, IDC_AG_IPS, 28, 432, 330, 150);
  lvAddColumn(ips_, 0, L"IP / CIDR", 300);
  makeButton(panel_, IDC_AG_ADDIP, L"Add IP…", 28, 588, 150, 28);
  makeButton(panel_, IDC_AG_DELIP, L"Remove", 184, 588, 120, 28);

  makeLabel(panel_, L"Access (SIDs / users / groups)", 384, 410, 340, 20);
  access_ = makeListView(panel_, IDC_AG_ACCESS, 384, 432, 344, 150);
  lvAddColumn(access_, 0, L"Type", 70);
  lvAddColumn(access_, 1, L"Value", 250);
  makeButton(panel_, IDC_AG_ADDPRIN, L"Add user/group…", 384, 588, 170, 28);
  makeButton(panel_, IDC_AG_DELPRIN, L"Remove", 560, 588, 120, 28);

  makeLabel(panel_, L"Port", 28, 626, 40, 20);
  port_ = makeEdit(panel_, IDC_AG_PORT, 72, 622, 70, 24, ES_NUMBER);
  makeLabel(panel_, L"Listen", 156, 626, 48, 20);
  listen_ = makeEdit(panel_, IDC_AG_LISTEN, 208, 622, 150, 24);
  tray_ = makeCheckbox(panel_, IDC_AG_TRAY, L"Tray icon", 384, 624, 110, 22);
  makeButton(panel_, IDC_AG_SAVE, L"Save config", 560, 622, 168, 28);

  applyGuiFont(panel_);
  refresh();
  return panel_;
}

void AgentPanel::onShow() { refresh(); }

void AgentPanel::refresh() {
  info_ = locateAgent();
  configPath_ = joinPath(info_.configDir, L"config.json");

  std::wstring s = L"Installed: " + std::wstring(info_.installed ? L"yes" : L"no") +
                   L"    Service: " + stateName(info_.state) + L"\nExe: " +
                   (info_.exePath.empty() ? L"(not found)" : info_.exePath) + L"\nVersion: " +
                   (info_.exeVersion.empty() ? L"?" : info_.exeVersion) + L"    Config: " +
                   info_.configDir;
  SetWindowTextW(status_, s.c_str());

  // Live sessions from status-*.json.
  ListView_DeleteAllItems(sessions_);
  LiveStatus live = aggregateStatus(info_.configDir);
  for (const auto& sess : live.sessions)
    lvAddRow(sessions_, sess.user,
             {utf8ToW(sess.ip), utf8ToW(sess.mode), fmtTime(sess.sinceUnix)});

  // Config.
  doc_ = ConfigDoc::load(wToUtf8(configPath_));
  allowed_.clear();
  for (auto& ip : doc_.allowedClients()) allowed_.push_back(utf8ToW(ip));
  sids_.clear();
  for (auto& v : doc_.accessSids()) sids_.push_back(utf8ToW(v));
  users_.clear();
  for (auto& v : doc_.accessUsers()) users_.push_back(utf8ToW(v));
  groups_.clear();
  for (auto& v : doc_.accessGroups()) groups_.push_back(utf8ToW(v));

  ListView_DeleteAllItems(ips_);
  for (auto& ip : allowed_) lvAddRow(ips_, ip);
  rebuildAccessList();

  SetWindowTextW(port_, std::to_wstring(doc_.port()).c_str());
  SetWindowTextW(listen_, utf8ToW(doc_.listenAddress()).c_str());
  SendMessageW(tray_, BM_SETCHECK, doc_.trayIcon() ? BST_CHECKED : BST_UNCHECKED, 0);

  // Current cert paths.
  std::wstring c = L"CA/cert/key under " + info_.configDir + L" (set on import).";
  SetWindowTextW(certs_, c.c_str());
}

void AgentPanel::rebuildAccessList() {
  ListView_DeleteAllItems(access_);
  accessRows_.clear();
  for (size_t i = 0; i < sids_.size(); ++i) {
    lvAddRow(access_, L"SID", {sids_[i]});
    accessRows_.push_back({0, i});
  }
  for (size_t i = 0; i < users_.size(); ++i) {
    lvAddRow(access_, L"User", {users_[i]});
    accessRows_.push_back({1, i});
  }
  for (size_t i = 0; i < groups_.size(); ++i) {
    lvAddRow(access_, L"Group", {groups_[i]});
    accessRows_.push_back({2, i});
  }
}

void AgentPanel::doServiceAction(int id) {
  bool ok = false;
  std::wstring verb;
  switch (id) {
    case IDC_AG_INSTALL: {
      std::wstring exe = info_.exePath;
      if (exe.empty() &&
          !openFileDialog(parent_, L"Locate pvnc-agent.exe", L"Executable\0*.exe\0", exe))
        return;
      ok = installServiceAt(exe, L"--service") == 0;
      verb = L"Install";
      break;
    }
    case IDC_AG_UNINSTALL: ok = uninstallService() == 0; verb = L"Uninstall"; break;
    case IDC_AG_START: ok = startService(kServiceName); verb = L"Start"; break;
    case IDC_AG_STOP: ok = stopService(kServiceName); verb = L"Stop"; break;
    case IDC_AG_RESTART: ok = restartService(kServiceName); verb = L"Restart"; break;
  }
  if (!ok)
    warn(parent_, verb + L" failed. The manager may need to run as administrator.");
  refresh();
}

void AgentPanel::doImportBundle() {
  std::wstring dir;
  if (!pickFolder(parent_, L"Choose the bundle folder (ca.pem, *-cert.pem, *-key.pem)", dir))
    return;
  // Accept either agent-* or client-* leaf names.
  std::wstring certSrc = joinPath(dir, L"agent-cert.pem");
  std::wstring keySrc = joinPath(dir, L"agent-key.pem");
  if (GetFileAttributesW(certSrc.c_str()) == INVALID_FILE_ATTRIBUTES) {
    certSrc = joinPath(dir, L"client-cert.pem");
    keySrc = joinPath(dir, L"client-key.pem");
  }
  std::wstring caSrc = joinPath(dir, L"ca.pem");
  std::wstring caDst = joinPath(info_.configDir, L"ca.pem");
  std::wstring certDst = joinPath(info_.configDir, L"agent-cert.pem");
  std::wstring keyDst = joinPath(info_.configDir, L"agent-key.pem");
  if (!CopyFileW(caSrc.c_str(), caDst.c_str(), FALSE) ||
      !CopyFileW(certSrc.c_str(), certDst.c_str(), FALSE) ||
      !CopyFileW(keySrc.c_str(), keyDst.c_str(), FALSE)) {
    warn(parent_, L"Failed to copy bundle files into\n" + info_.configDir +
                      L"\n(check the folder contents and that you can write there).");
    return;
  }
  doc_ = ConfigDoc::load(wToUtf8(configPath_));
  doc_.setTls(wToUtf8(certDst), wToUtf8(keyDst), wToUtf8(caDst));
  if (!doc_.save(wToUtf8(configPath_))) {
    warn(parent_, L"Copied certs but could not write config.json.");
    return;
  }
  info(parent_, L"Bundle imported and config.json updated.");
  refresh();
}

void AgentPanel::doAddIp() {
  std::wstring ip;
  if (!promptText(parent_, L"Add allowed IP", L"IP or CIDR", ip)) return;
  if (!validIpv4OrCidr(ip)) { warn(parent_, L"Not a valid IPv4 address or a.b.c.d/n range."); return; }
  allowed_.push_back(ip);
  ListView_DeleteAllItems(ips_);
  for (auto& e : allowed_) lvAddRow(ips_, e);
}

void AgentPanel::doRemoveSelected(HWND lv, std::vector<std::wstring>* backing) {
  int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
  if (sel < 0) return;
  backing->erase(backing->begin() + sel);
  ListView_DeleteAllItems(lv);
  for (auto& e : *backing) lvAddRow(lv, e);
}

void AgentPanel::doAddPrincipal() {
  std::vector<Principal> picked;
  if (!pickPrincipals(parent_, picked)) return;
  for (auto& p : picked) sids_.push_back(p.sid);  // object-picker choices stored as SIDs
  rebuildAccessList();
}

void AgentPanel::doRemovePrincipal() {
  int sel = ListView_GetNextItem(access_, -1, LVNI_SELECTED);
  if (sel < 0 || sel >= (int)accessRows_.size()) return;
  auto [list, idx] = accessRows_[sel];
  std::vector<std::wstring>* v = list == 0 ? &sids_ : (list == 1 ? &users_ : &groups_);
  if (idx < v->size()) v->erase(v->begin() + idx);
  rebuildAccessList();
}

void AgentPanel::doSaveConfig() {
  doc_ = ConfigDoc::load(wToUtf8(configPath_));  // reload to preserve unknown keys
  std::vector<std::string> ip, sid, usr, grp;
  for (auto& e : allowed_) ip.push_back(wToUtf8(e));
  for (auto& e : sids_) sid.push_back(wToUtf8(e));
  for (auto& e : users_) usr.push_back(wToUtf8(e));
  for (auto& e : groups_) grp.push_back(wToUtf8(e));
  doc_.setAllowedClients(ip);
  doc_.setAccessSids(sid);
  doc_.setAccessUsers(usr);
  doc_.setAccessGroups(grp);
  int p = _wtoi(getWindowText(port_).c_str());
  if (p > 0 && p < 65536) doc_.setPort((uint16_t)p);
  doc_.setListenAddress(wToUtf8(getWindowText(listen_)));
  doc_.setTrayIcon(SendMessageW(tray_, BM_GETCHECK, 0, 0) == BST_CHECKED);
  if (!doc_.save(wToUtf8(configPath_))) {
    warn(parent_, L"Could not write\n" + configPath_ + L"\n(run as administrator?).");
    return;
  }
  info(parent_, L"Saved " + configPath_);
}

void AgentPanel::onCommand(int id) {
  switch (id) {
    case IDC_AG_REFRESH: refresh(); break;
    case IDC_AG_INSTALL:
    case IDC_AG_UNINSTALL:
    case IDC_AG_START:
    case IDC_AG_STOP:
    case IDC_AG_RESTART: doServiceAction(id); break;
    case IDC_AG_IMPORT: doImportBundle(); break;
    case IDC_AG_ADDIP: doAddIp(); break;
    case IDC_AG_DELIP: doRemoveSelected(ips_, &allowed_); break;
    case IDC_AG_ADDPRIN: doAddPrincipal(); break;
    case IDC_AG_DELPRIN: doRemovePrincipal(); break;
    case IDC_AG_SAVE: doSaveConfig(); break;
  }
}

}  // namespace pvnc
