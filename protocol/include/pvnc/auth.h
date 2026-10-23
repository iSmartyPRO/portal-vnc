#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "pvnc/handshake.h"

namespace pvnc {

enum class AuthOutcome {
  Ok,              // valid credentials AND authorized
  BadCredentials,  // logon failed
  NotAdmin,        // logged on but not authorized by any rule
};

// Authorization rules beyond the built-in Administrators group. All are
// optional and additive; names are resolved to SIDs via LookupAccountName, so
// human-readable forms like "CORP\\Helpdesk" or "jdoe" work (any locale).
struct AccessPolicy {
  std::vector<std::wstring> sids;    // raw SIDs (SDDL form)
  std::vector<std::wstring> users;   // user names -> allowed if the logon IS that user
  std::vector<std::wstring> groups;  // group names -> allowed if a token member
};

// Validates the credentials via LogonUserW (NETWORK logon) and authorizes the
// resulting token: allowed if it is a member of the built-in Administrators
// group (SID S-1-5-32-544), or a member of any listed group/SID, or is one of
// the listed users. Works regardless of Windows UI language.
AuthOutcome authenticate(const Credentials& creds, const AccessPolicy& policy = {});

// True if the token's user SID equals the SID of the named account.
bool tokenIsUser(HANDLE token, const std::wstring& accountName);
// Resolves an account or group name to a SID and checks token membership.
bool tokenInNamedGroup(HANDLE token, const std::wstring& groupName);

// Testable helper: does the given access token contain the given SID?
bool tokenHasSid(HANDLE token, PSID sid);

// Maps an AuthOutcome to the wire RefuseCode.
RefuseCode toRefuse(AuthOutcome o);

}  // namespace pvnc
