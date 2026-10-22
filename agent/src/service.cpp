#include "service.h"

#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")

namespace pvnc {

namespace {

SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status{};
HANDLE g_stopEvent = nullptr;
HANDLE g_sessionEvent = nullptr;

std::wstring exePath() {
  wchar_t buf[MAX_PATH];
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return buf;
}

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

// Launches the agent as SYSTEM in `sessionId` on the interactive desktop.
// Returns the process handle (caller closes), or nullptr on failure.
HANDLE launchWorker(DWORD sessionId) {
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

  std::wstring cmd = L"\"" + exePath() + L"\" --worker";
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

void supervise() {
  HANDLE worker = nullptr;
  for (;;) {
    DWORD session = WTSGetActiveConsoleSessionId();
    if (session != 0xFFFFFFFF) {
      if (worker) { TerminateProcess(worker, 0); CloseHandle(worker); worker = nullptr; }
      worker = launchWorker(session);
    }

    HANDLE waits[3] = {g_stopEvent, g_sessionEvent, worker};
    DWORD count = worker ? 3 : 2;
    DWORD r = WaitForMultipleObjects(count, waits, FALSE,
                                     worker ? INFINITE : 3000);
    if (r == WAIT_OBJECT_0) break;                 // stop
    if (r == WAIT_OBJECT_0 + 1) { ResetEvent(g_sessionEvent); continue; }  // session change
    if (worker && r == WAIT_OBJECT_0 + 2) {        // worker died: relaunch shortly
      CloseHandle(worker); worker = nullptr;
      Sleep(1000);
      continue;
    }
    // timeout with no worker: loop and retry to find a session
  }
  if (worker) { TerminateProcess(worker, 0); CloseHandle(worker); }
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

int installService() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
  if (!scm) { std::fprintf(stderr, "OpenSCManager failed (%lu). Run as admin.\n", GetLastError()); return 1; }

  std::wstring bin = L"\"" + exePath() + L"\" --service";
  SC_HANDLE svc = CreateServiceW(
      scm, kServiceName, kServiceDisplay, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
      SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, bin.c_str(), nullptr, nullptr, nullptr,
      nullptr /* LocalSystem */, nullptr);
  if (!svc) {
    DWORD e = GetLastError();
    CloseServiceHandle(scm);
    if (e == ERROR_SERVICE_EXISTS) { std::fprintf(stderr, "Service already installed.\n"); return 0; }
    std::fprintf(stderr, "CreateService failed (%lu).\n", e);
    return 1;
  }
  SERVICE_DESCRIPTIONW desc{const_cast<wchar_t*>(L"PortalVNC secure remote access agent.")};
  ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);
  std::printf("Service '%ls' installed. Start with: sc start %ls\n", kServiceName, kServiceName);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return 0;
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
