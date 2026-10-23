#pragma once
#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "agentlocate.h"
#include "configedit.h"

namespace pvnc {

// The Agent role: show status/version/sessions, control the service, import a
// certificate bundle, and edit the allow-list + access lists (users/groups via
// the object picker, stored as SIDs) and port/listen/tray in config.json.
class AgentPanel {
 public:
  HWND create(HWND parent);
  HWND hwnd() const { return panel_; }
  void onCommand(int id);
  void onShow();  // refresh when the tab becomes visible

  static void commandThunk(void* owner, int id);

 private:
  void refresh();
  void rebuildAccessList();
  void doServiceAction(int id);
  void doImportBundle();
  void doAddIp();
  void doRemoveSelected(HWND lv, std::vector<std::wstring>* backing);
  void doAddPrincipal();
  void doRemovePrincipal();
  void doSaveConfig();

  HWND panel_ = nullptr;
  HWND parent_ = nullptr;
  HWND status_ = nullptr;
  HWND certs_ = nullptr;
  HWND sessions_ = nullptr;
  HWND ips_ = nullptr;
  HWND access_ = nullptr;
  HWND port_ = nullptr;
  HWND listen_ = nullptr;
  HWND tray_ = nullptr;

  AgentInstall info_;
  ConfigDoc doc_;
  std::wstring configPath_;

  std::vector<std::wstring> allowed_;
  std::vector<std::wstring> sids_;
  std::vector<std::wstring> users_;
  std::vector<std::wstring> groups_;
  // Row -> (list: 0=sid,1=user,2=group, index within that list) for removal.
  std::vector<std::pair<int, size_t>> accessRows_;
};

}  // namespace pvnc
