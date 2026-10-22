#include "pvnc/auth.h"

#include <sddl.h>

#pragma comment(lib, "advapi32.lib")

namespace pvnc {

RefuseCode toRefuse(AuthOutcome o) {
  switch (o) {
    case AuthOutcome::Ok: return RefuseCode::None;
    case AuthOutcome::BadCredentials: return RefuseCode::AuthFailed;
    case AuthOutcome::NotAdmin: return RefuseCode::AuthNotAdmin;
  }
  return RefuseCode::AuthFailed;
}

bool tokenHasSid(HANDLE token, PSID sid) {
  if (!sid || !IsValidSid(sid)) return false;
  BOOL isMember = FALSE;
  // CheckTokenMembership with a NULL token checks the calling thread's token;
  // here we pass an explicit (primary) token, which CheckTokenMembership
  // accepts only if it is an impersonation token. Use the duplicated
  // impersonation token path for robustness.
  HANDLE imp = nullptr;
  if (!DuplicateToken(token, SecurityImpersonation, &imp)) {
    // Fall back: some tokens are already impersonation tokens.
    if (!CheckTokenMembership(token, sid, &isMember)) return false;
    return isMember == TRUE;
  }
  BOOL ok = CheckTokenMembership(imp, sid, &isMember);
  CloseHandle(imp);
  return ok && isMember == TRUE;
}

namespace {

// Builds the well-known built-in Administrators SID (S-1-5-32-544).
struct AdminSid {
  BYTE buf[SECURITY_MAX_SID_SIZE];
  PSID sid = nullptr;
  AdminSid() {
    DWORD cb = sizeof(buf);
    if (CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, buf, &cb)) {
      sid = reinterpret_cast<PSID>(buf);
    }
  }
};

}  // namespace

AuthOutcome authenticate(const Credentials& creds,
                         const std::vector<std::wstring>& extraSids) {
  HANDLE token = nullptr;
  const wchar_t* domain = creds.domain.empty() ? nullptr : creds.domain.c_str();
  BOOL ok = LogonUserW(creds.user.c_str(), domain, creds.password.c_str(),
                       LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &token);
  if (!ok || !token) {
    return AuthOutcome::BadCredentials;
  }

  AuthOutcome result = AuthOutcome::NotAdmin;

  AdminSid admin;
  if (admin.sid && tokenHasSid(token, admin.sid)) {
    result = AuthOutcome::Ok;
  } else {
    for (const auto& s : extraSids) {
      PSID extra = nullptr;
      if (ConvertStringSidToSidW(s.c_str(), &extra)) {
        bool has = tokenHasSid(token, extra);
        LocalFree(extra);
        if (has) {
          result = AuthOutcome::Ok;
          break;
        }
      }
    }
  }

  CloseHandle(token);
  return result;
}

}  // namespace pvnc
