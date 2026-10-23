#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace pvnc {

// A user or group chosen from the Windows object picker.
struct Principal {
  std::wstring name;  // display name, e.g. "CORP\\jdoe" or "jdoe"
  std::wstring sid;   // SDDL string SID, e.g. "S-1-5-21-..."
  bool isGroup = false;
};

// Opens the Windows DS Object Picker for users and groups (local + domain,
// multi-select) and appends the chosen principals to `out`. Returns true if the
// user picked at least one, false on cancel or error. Each principal's SID is
// taken from the picker's fetched objectSid when available, else resolved via
// LookupAccountNameW.
bool pickPrincipals(HWND owner, std::vector<Principal>& out);

}  // namespace pvnc
