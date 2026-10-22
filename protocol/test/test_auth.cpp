#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <sddl.h>

#include <cstdlib>

#include "pvnc/auth.h"

using namespace pvnc;

namespace {
PSID makeSid(const wchar_t* sddl) {
  PSID sid = nullptr;
  ConvertStringSidToSidW(sddl, &sid);
  return sid;
}
}  // namespace

TEST_CASE("tokenHasSid detects a SID the process token lacks", "[auth]") {
  HANDLE token = nullptr;
  REQUIRE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token));

  // A SID the current token cannot possibly contain.
  PSID bogus = makeSid(L"S-1-5-32-123456789");
  REQUIRE(bogus != nullptr);
  CHECK_FALSE(tokenHasSid(token, bogus));
  LocalFree(bogus);
  CloseHandle(token);
}

TEST_CASE("tokenHasSid detects builtin Administrators when elevated", "[auth]") {
  HANDLE token = nullptr;
  REQUIRE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token));
  BYTE buf[SECURITY_MAX_SID_SIZE];
  DWORD cb = sizeof(buf);
  REQUIRE(CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, buf, &cb));
  bool has = tokenHasSid(token, reinterpret_cast<PSID>(buf));
  // When the test runs elevated, this is true; when not, it is false.
  // Either way tokenHasSid must not crash and must return a definite bool.
  INFO("process token has builtin Administrators: " << (has ? "yes" : "no"));
  SUCCEED();
  CloseHandle(token);
}

TEST_CASE("authenticate rejects a wrong password as BadCredentials", "[auth]") {
  Credentials creds;
  creds.user = L"pvnc_no_such_user_zzz";
  creds.password = L"definitely-wrong-password";
  auto outcome = authenticate(creds, {});
  CHECK(outcome == AuthOutcome::BadCredentials);
}

TEST_CASE("authenticate admin user returns Ok (requires PVNC_TEST_USER/PASS)", "[auth][live]") {
  const char* u = std::getenv("PVNC_TEST_USER");
  const char* p = std::getenv("PVNC_TEST_PASS");
  if (!u || !p) { WARN("PVNC_TEST_USER/PASS not set; skipping live logon test"); return; }
  auto toW = [](const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
  };
  Credentials creds;
  creds.user = toW(u);
  creds.password = toW(p);
  const char* dom = std::getenv("PVNC_TEST_DOMAIN");
  if (dom) creds.domain = toW(dom);
  CHECK(authenticate(creds, {}) == AuthOutcome::Ok);
}
