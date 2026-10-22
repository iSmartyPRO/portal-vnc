#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "pvnc/handshake.h"

namespace pvnc {

enum class AuthOutcome {
  Ok,              // valid credentials AND member of an allowed group
  BadCredentials,  // logon failed
  NotAdmin,        // logged on but not in the Administrators (or extra) group
};

// Validates the credentials via LogonUserW (NETWORK logon) and checks that the
// resulting token is a member of the built-in Administrators group
// (SID S-1-5-32-544) or any SID in extraSids (SDDL string form, e.g.
// "S-1-5-21-...-1234"). Works regardless of Windows UI language.
AuthOutcome authenticate(const Credentials& creds,
                         const std::vector<std::wstring>& extraSids = {});

// Testable helper: does the given access token contain the given SID?
bool tokenHasSid(HANDLE token, PSID sid);

// Maps an AuthOutcome to the wire RefuseCode.
RefuseCode toRefuse(AuthOutcome o);

}  // namespace pvnc
