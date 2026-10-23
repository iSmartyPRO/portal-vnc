#include "service.h"

#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>

#include "config.h"
#include "paths.h"

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")

namespace pvnc {

namespace {

SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status{};
HANDLE g_stopEvent = nullptr;
HANDLE g_sessionEvent = nullptr;

void reportStatus(DWORD state, DWORD waitHint = 0) {
  g_status.dwCurrentState = state;
  g_status.dwWaitHint = waitHint;
  g_status.dwControlsAccepted =
      (state == SERVICE_START_PENDING)
          ? 0
          : (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE);
  static DWORD checkPoint = 1;
  g_status.dwCheckPoint =
      (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : checkPoint++;
  SetServiceStatus(g_statusHandle, &g_status);
}

// Base listening port from config.json next to the exe (default 7654).
uint16_t basePort() {
  try {
    AgentConfig c = loadConfig(exeDirUtf8() + "\\config.json");
    return c.port;
  } catch (...) { return 7654; }
}

// Launches the agent as SYSTEM in `sessionId` on the interactive desktop,
// listening on `port`. Returns the process handle (caller closes), or nullptr.
HANDLE launchWorker(DWORD sessionId, uint16_t port) {
  HANDLE procToken = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY,
                        &procToken))
    return nullptr;

  HANDLE dup = nullptr;
  if (!DuplicateTokenEx(procToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation,
                        TokenPrimary, &dup)) {
    CloseHandle(procToken);
    return nullptr;
  }
  CloseHandle(procToken);

  if (!SetTokenInformation(dup, TokenSessionId, &sessionId, sizeof(sessionId))) {
    // Non-fatal on some configurations; continue.
  }

  LPVOID env = nullptr;
  CreateEnvironmentBlock(&env, dup, FALSE);

  std::wstring cmd = L"\"" + exePathW() + L"\" --worker --port " + std::to_wstring(port);
  std::wstring desktop = L"winsta0\\default";

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.lpDesktop = const_cast<wchar_t*>(desktop.c_str());
  PROCESS_INFORMATION pi{};

  std::wstring mutableCmd = cmd;
  BOOL ok = CreateProcessAsUserW(
      dup, nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
      CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, env, nullptr, &si, &pi);

  if (env) DestroyEnvironmentBlock(env);
  CloseHandle(dup);
  if (!ok) return nullptr;
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

DWORD WINAPI handlerEx(DWORD control, DWORD eventType, LPVOID, LPVOID) {
  switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
      reportStatus(SERVICE_STOP_PENDING, 5000);
      SetEvent(g_stopEvent);
      return NO_ERROR;
    case SERVICE_CONTROL_SESSIONCHANGE:
      // Any console connect/disconnect/logon/logoff: relaunch in the new session.
      (void)eventType;
      SetEvent(g_sessionEvent);
      return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
      return NO_ERROR;
    default:
      return ERROR_CALL_NOT_IMPLEMENTED;
  }
}

// The set of sessions we should be serving right now: the active console
// session plus any connected/active RDP sessions that have a logged-in user.
std::set<DWORD> targetSessions() {
  std::set<DWORD> out;
  DWORD console = WTSGetActiveConsoleSessionId();
  if (console != 0xFFFFFFFF) out.insert(console);

  PWTS_SESSION_INFOW info = nullptr;
  DWORD count = 0;
  if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &info, &count)) {
    for (DWORD i = 0; i < count; ++i) {
      if (info[i].State == WTSActive) out.insert(info[i].SessionId);
    }
    WTSFreeMemory(info);
  }
  return out;
}

// Port for a session: the base port for the active console session, base+id
// for additional (RDP) sessions, so the common single-user case stays on base.
uint16_t portForSession(DWORD sessionId, DWORD console, uint16_t base) {
  if (sessionId == console) return base;
  return uint16_t(base + sessionId);
}

void supervise() {
  const uint16_t base = basePort();
  std::map<DWORD, HANDLE> workers;  // sessionId -> process

  auto reconcile = [&]() {
    DWORD console = WTSGetActiveConsoleSessionId();
    std::set<DWORD> want = targetSessions();

    // Drop workers for sessions that are gone or whose process exited.
    for (auto it = workers.begin(); it != workers.end();) {
      bool dead = WaitForSingleObject(it->second, 0) == WAIT_OBJECT_0;
      if (want.find(it->first) == want.end() || dead) {
        if (!dead) TerminateProcess(it->second, 0);
        CloseHandle(it->second);
        it = workers.erase(it);
      } else {
        ++it;
      }
    }
    // Start workers for sessions we want but don't have.
    for (DWORD s : want) {
      if (workers.find(s) == workers.end()) {
        HANDLE h = launchWorker(s, portForSession(s, console, base));
        if (h) workers[s] = h;
      }
    }
  };

  for (;;) {
    reconcile();
    HANDLE waits[2] = {g_stopEvent, g_sessionEvent};
    DWORD r = WaitForMultipleObjects(2, waits, FALSE, 5000);
    if (r == WAIT_OBJECT_0) break;                      // stop
    if (r == WAIT_OBJECT_0 + 1) ResetEvent(g_sessionEvent);  // session change -> reconcile
    // timeout -> periodic reconcile (also catches crashed workers)
  }
  for (auto& kv : workers) { TerminateProcess(kv.second, 0); CloseHandle(kv.second); }
}

void WINAPI serviceMain(DWORD, LPWSTR*) {
  g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, handlerEx, nullptr);
  if (!g_statusHandle) return;
  g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  reportStatus(SERVICE_START_PENDING, 3000);

  g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  g_sessionEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

  reportStatus(SERVICE_RUNNING);
  supervise();
  reportStatus(SERVICE_STOPPED);
}

}  // namespace

int installServiceAt(const std::wstring& exePath, const std::wstring& args) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
  if (!scm) return 1;
  std::wstring bin = L"\"" + exePath + L"\"";
  if (!args.empty()) bin += L" " + args;
  SC_HANDLE svc = CreateServiceW(
      scm, kServiceName, kServiceDisplay, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
      SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, bin.c_str(), nullptr, nullptr, nullptr,
      nullptr /* LocalSystem */, nullptr);
  if (!svc) {
    DWORD e = GetLastError();
    CloseServiceHandle(scm);
    return (e == ERROR_SERVICE_EXISTS) ? 0 : 1;
  }
  SERVICE_DESCRIPTIONW desc{const_cast<wchar_t*>(L"PortalVNC secure remote access agent.")};
  ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return 0;
}

int installService() {
  int rc = installServiceAt(exePathW(), L"--service");
  if (rc == 0) std::printf("Service '%ls' installed. Start with: sc start %ls\n", kServiceName, kServiceName);
  else std::fprintf(stderr, "CreateService failed (run as admin).\n");
  return rc;
}

SvcState queryService(const std::wstring& name) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return SvcState::NotInstalled;
  SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_QUERY_STATUS);
  if (!svc) { CloseServiceHandle(scm); return SvcState::NotInstalled; }
  SERVICE_STATUS st{};
  SvcState out = SvcState::Other;
  if (QueryServiceStatus(svc, &st)) {
    switch (st.dwCurrentState) {
      case SERVICE_RUNNING: out = SvcState::Running; break;
      case SERVICE_STOPPED: out = SvcState::Stopped; break;
      case SERVICE_START_PENDING: out = SvcState::StartPending; break;
      case SERVICE_STOP_PENDING: out = SvcState::StopPending; break;
      default: out = SvcState::Other; break;
    }
  }
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return out;
}

static bool waitState(SC_HANDLE svc, DWORD want, DWORD timeoutMs) {
  SERVICE_STATUS st{};
  DWORD waited = 0;
  while (QueryServiceStatus(svc, &st)) {
    if (st.dwCurrentState == want) return true;
    if (waited >= timeoutMs) return false;
    Sleep(200); waited += 200;
  }
  return false;
}

bool startService(const std::wstring& name) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
  bool ok = false;
  if (svc) {
    if (StartServiceW(svc, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
      ok = waitState(svc, SERVICE_RUNNING, 15000);
    CloseServiceHandle(svc);
  }
  CloseServiceHandle(scm);
  return ok;
}

bool stopService(const std::wstring& name) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
  bool ok = false;
  if (svc) {
    SERVICE_STATUS st{};
    if (ControlService(svc, SERVICE_CONTROL_STOP, &st) || GetLastError() == ERROR_SERVICE_NOT_ACTIVE)
      ok = waitState(svc, SERVICE_STOPPED, 15000);
    CloseServiceHandle(svc);
  }
  CloseServiceHandle(scm);
  return ok;
}

bool restartService(const std::wstring& name) {
  stopService(name);
  return startService(name);
}

int uninstallService() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (!scm) { std::fprintf(stderr, "OpenSCManager failed (%lu). Run as admin.\n", GetLastError()); return 1; }
  SC_HANDLE svc = OpenServiceW(scm, kServiceName, SERVICE_STOP | DELETE);
  if (!svc) { CloseServiceHandle(scm); std::fprintf(stderr, "Service not found.\n"); return 1; }
  SERVICE_STATUS st{};
  ControlService(svc, SERVICE_CONTROL_STOP, &st);
  BOOL ok = DeleteService(svc);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  std::printf(ok ? "Service removed.\n" : "DeleteService failed.\n");
  return ok ? 0 : 1;
}

int runService() {
  SERVICE_TABLE_ENTRYW table[] = {
      {const_cast<wchar_t*>(kServiceName), serviceMain}, {nullptr, nullptr}};
  if (!StartServiceCtrlDispatcherW(table)) {
    std::fprintf(stderr, "StartServiceCtrlDispatcher failed (%lu). Use --install then 'sc start'.\n",
                 GetLastError());
    return 1;
  }
  return 0;
}

}  // namespace pvnc
