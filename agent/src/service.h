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

}  // namespace pvnc
