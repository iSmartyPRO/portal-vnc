#pragma once
#include <string>

namespace pvnc {

// Windows service lifecycle for the PortalVNC agent.
// The service (LocalSystem, session 0) is a supervisor: it launches the agent
// as a "worker" (SYSTEM) in the active console session on winsta0\default and
// relaunches it on session change. The worker does all the RFB/capture/input.

constexpr const wchar_t* kServiceName = L"PortalVNC";
constexpr const wchar_t* kServiceDisplay = L"PortalVNC Agent";

// Returns 0 on success. Registers/removes the service in the SCM.
int installService();
int uninstallService();

// Entry point when started by the SCM (binary path uses "--service").
// Blocks until the service stops.
int runService();

// --- Reusable service control (shared with pvnc-admin) ---

enum class SvcState { NotInstalled, Stopped, Running, StartPending, StopPending, Other };

// Registers a service whose ImagePath is "<exePath>" <args>, auto-start,
// LocalSystem. Returns 0 on success (ERROR_SERVICE_EXISTS is treated as 0).
int installServiceAt(const std::wstring& exePath, const std::wstring& args);

// Current state of the named service (NotInstalled if it does not exist).
SvcState queryService(const std::wstring& name);
bool startService(const std::wstring& name);
bool stopService(const std::wstring& name);
bool restartService(const std::wstring& name);

}  // namespace pvnc
