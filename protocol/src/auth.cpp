#include "pvnc/auth.h"

#include <sddl.h>

#include <cstdlib>
#include <vector>

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

// Resolves an account/group name (e.g. "CORP\\Helpdesk", "jdoe", ".\\Admins")
// to a SID. Caller frees with free(). Returns nullptr if unknown.
PSID lookupSid(const std::wstring& name) {
  DWORD cbSid = 0, cbDom = 0;
  SID_NAME_USE use;
  LookupAccountNameW(nullptr, name.c_str(), nullptr, &cbSid, nullptr, &cbDom, &use);
  if (cbSid == 0) return nullptr;
  PSID sid = static_cast<PSID>(malloc(cbSid));
  std::vector<wchar_t> dom(cbDom ? cbDom : 1);
  if (!sid) return nullptr;
  if (!LookupAccountNameW(nullptr, name.c_str(), sid, &cbSid, dom.data(), &cbDom, &use)) {
    free(sid);
    return nullptr;
  }
  return sid;
}

}  // namespace

bool tokenInNamedGroup(HANDLE token, const std::wstring& groupName) {
  PSID sid = lookupSid(groupName);
  if (!sid) return false;
  bool has = tokenHasSid(token, sid);
  free(sid);
  return has;
}

bool tokenIsUser(HANDLE token, const std::wstring& accountName) {
  PSID want = lookupSid(accountName);
  if (!want) return false;
  DWORD cb = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &cb);
  std::vector<BYTE> buf(cb ? cb : 1);
  bool eq = false;
  if (cb && GetTokenInformation(token, TokenUser, buf.data(), cb, &cb)) {
    PSID userSid = reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid;
    eq = EqualSid(userSid, want) == TRUE;
  }
  free(want);
  return eq;
}

AuthOutcome authenticate(const Credentials& creds, const AccessPolicy& policy) {
  HANDLE token = nullptr;
  const wchar_t* domain = creds.domain.empty() ? nullptr : creds.domain.c_str();
  BOOL ok = LogonUserW(creds.user.c_str(), domain, creds.password.c_str(),
                       LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &token);
  if (!ok || !token) {
    return AuthOutcome::BadCredentials;
  }

  AuthOutcome result = AuthOutcome::NotAdmin;

  AdminSid admin;
  auto authorized = [&]() -> bool {
    if (admin.sid && tokenHasSid(token, admin.sid)) return true;  // built-in Administrators
    for (const auto& g : policy.groups) if (tokenInNamedGroup(token, g)) return true;
    for (const auto& u : policy.users) if (tokenIsUser(token, u)) return true;
    for (const auto& s : policy.sids) {
      PSID extra = nullptr;
      if (ConvertStringSidToSidW(s.c_str(), &extra)) {
        bool has = tokenHasSid(token, extra);
        LocalFree(extra);
        if (has) return true;
      }
    }
    return false;
  };
  if (authorized()) result = AuthOutcome::Ok;

  CloseHandle(token);
  return result;
}

}  // namespace pvnc
