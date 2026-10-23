#pragma once
#include <windows.h>

#include <string>

namespace pvnc {

// Parses the system-info JSON from the agent and shows it in a read-only window.
void showSystemInfo(HWND parent, const std::string& json);

}  // namespace pvnc
