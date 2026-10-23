# PortalVNC Agent Manager — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone native GUI utility (`pvnc-admin.exe`) that runs a central CA on the admin's PC and configures the agent on each machine, plus the agent-side enablers it needs (version resource, session registry, status files, reusable service control).

**Architecture:** One C++/Win32 exe with two roles (Certificate Authority, Agent setup). Certificates are made with mbedTLS; the CA key is stored in a password-encrypted container (AES-256-GCM + PBKDF2). Config is edited at the JSON-tree level (preserving unknown keys). Status is read from per-worker `status-<port>.json` files the agent writes. The service is controlled through a reusable module shared with the agent.

**Tech Stack:** C++17, MSVC, `x64-windows-static`, mbedTLS 3.6.5, nlohmann-json, Win32 (SCM, DS Object Picker, GetFileVersionInfo), Catch2 for unit tests.

**Spec:** `docs/superpowers/specs/2026-10-23-agent-manager-design.md`

## Global Constraints

- Windows x64, C++17, static linking, no .NET/openssl runtime. mbedTLS + Win32 only.
- Agent/config/service code and the `C:\ProgramData\PortalVNC` layout are reused; config file format must stay readable by `agent/src/config.cpp`.
- CA private key never leaves the admin PC and is never written unencrypted to disk.
- `allowedClients` is IPv4 exact-or-CIDR only (the agent matches only IPv4).
- Access picks from the object picker are stored as SIDs in `access.sids`.
- UI follows the viewer's premium dark style; own icon from `assets/`.
- Version string comes from one source: CMake `PVNC_VERSION` (from the repo `VERSION`/`vcpkg.json`).

## Review Focus

- **Wrong CA password** → decrypt fails cleanly with a clear message, never a crash or a half-read key (Task 5).
- **Config file with unknown/future keys or comments** → editing one field preserves all other keys (Task 6).
- **Agent not installed / service missing** → locator and status read report "not installed" instead of throwing (Task 7).
- **Two RDP sessions (two `status-<port>.json`)** → the manager aggregates both, not just one (Task 7).
- **Issued agent/client cert** → actually verifies against the CA and is accepted by a real agent (Task 4 + manual acceptance).

---

### Task 1: Version plumbing (agent exe version + agentVersion field)

**Files:**
- Modify: `CMakeLists.txt` (define `PVNC_VERSION` from `VERSION` file), `agent/src/pvnc_agent.rc` (add `VS_VERSION_INFO`), `agent/src/sysinfo.cpp` (add `agentVersion`)
- Create: `VERSION` (single line, e.g. `0.1.9`) if absent; `cmake/version.cmake` (reads VERSION → `PVNC_VERSION`)
- Test: `agent/test/test_sysinfo.cpp`

**Interfaces:**
- Produces: compile definition `PVNC_VERSION` ("x.y.z") available to agent sources and the `.rc`; `collectSystemInfoJson()` JSON gains top-level key `agentVersion`.

- [ ] **Step 1: Write failing test** in `agent/test/test_sysinfo.cpp`: `TEST_CASE("system info reports the agent version")` — parse `collectSystemInfoJson()`, assert `j["agentVersion"]` equals `PVNC_VERSION`.
- [ ] **Step 2:** Run it; expect FAIL (key absent / no define).
- [ ] **Step 3:** Add `cmake/version.cmake` reading `VERSION` into `PVNC_VERSION`; in root `CMakeLists.txt` `add_compile_definitions(PVNC_VERSION="${PVNC_VERSION}")`; add `#define PVNC_VERSION` fallback? No — rely on the define. In `sysinfo.cpp` set `j["agentVersion"] = PVNC_VERSION;`. Add a `VS_VERSION_INFO` block to `pvnc_agent.rc` using `PVNC_VERSION` (RC gets it via `-D` from CMake `target_compile_definitions` on the RC, or a generated `version.rc.h`).
- [ ] **Step 4:** Run the test; expect PASS. Also verify `GetFileVersionInfo` on the built `pvnc-agent.exe` returns the version (quick manual/powershell check).
- [ ] **Step 5: Commit** — `feat(agent): embed product version (resource + agentVersion)`

### Task 2: Reusable service control module

**Files:**
- Modify: `agent/src/service.h`, `agent/src/service.cpp`
- Test: `agent/test/test_service.cpp`

**Interfaces:**
- Produces:
  - `enum class SvcState { NotInstalled, Stopped, Running, StartPending, StopPending, Other };`
  - `int installServiceAt(const std::wstring& exePath, const std::wstring& args);`
  - `SvcState queryService(const std::wstring& name);`
  - `bool startService(const std::wstring& name);` `bool stopService(const std::wstring& name);` `bool restartService(const std::wstring& name);`
  - Existing `installService()` now calls `installServiceAt(exePathW(), L"--service")`.

- [ ] **Step 1: Write failing test** in `test_service.cpp`: `TEST_CASE("queryService returns Running for a known running service")` — `CHECK(queryService(L"Dnscache") == SvcState::Running);` and `TEST_CASE("queryService returns NotInstalled for a bogus name")` — `CHECK(queryService(L"pvnc_no_such_service") == SvcState::NotInstalled);`
- [ ] **Step 2:** Run; expect FAIL (symbols undefined).
- [ ] **Step 3:** Implement `queryService` (OpenService + QueryServiceStatus), `installServiceAt` (refactor from `installService`, binary path = `"exePath" args`), `start/stop/restart` (ControlService + StartService + waits). `installService()` delegates.
- [ ] **Step 4:** Run; expect PASS.
- [ ] **Step 5: Commit** — `refactor(agent): reusable service control (install-at-path, start/stop/restart/query)`

### Task 3: Session registry + status writer (agent)

**Files:**
- Create: `agent/src/sessions.h`, `agent/src/sessions.cpp`, `agent/src/status_writer.h`, `agent/src/status_writer.cpp`
- Modify: `agent/src/rfb_server.cpp` (register/unregister a session; start the status writer in `run()`)
- Test: `agent/test/test_sessions.cpp`

**Interfaces:**
- Produces:
  - `struct SessionInfo { std::wstring user; std::string ip; std::string mode; uint64_t sinceUnix; };`
  - `class SessionRegistry { uint64_t add(const SessionInfo&); void remove(uint64_t id); std::vector<SessionInfo> list(); };` (mutex-guarded)
  - `std::string sessionsStatusJson(SessionRegistry&, uint16_t port);` → `{agentVersion, startedAt, port, sessions:[...]}`
  - `class StatusWriter { StatusWriter(SessionRegistry&, uint16_t port, std::string dir); void start(); void stop(); };` writes `<dir>\status-<port>.json` every ~5s.
- Consumes: `SessionRegistry` from rfb_server; the authenticated user (captured before `hs.creds.wipe()`), peer `ip`, and mode.

- [ ] **Step 1: Write failing test** in `test_sessions.cpp`: `TEST_CASE("registry add/list/remove")` — add two SessionInfo, list size 2, remove one, list size 1; and `TEST_CASE("status json contains agentVersion, port and sessions")` — parse `sessionsStatusJson(reg, 7654)`, assert `agentVersion`, `port==7654`, `sessions.size()` matches.
- [ ] **Step 2:** Run; expect FAIL.
- [ ] **Step 3:** Implement `SessionRegistry` (mutex + map<id,SessionInfo> + counter), `sessionsStatusJson` (nlohmann), `StatusWriter` (thread, 5s loop, write temp+rename). In `rfb_server.cpp`: capture `hs.creds.user` into a local before wipe; after auth-OK, `uint64_t sid = reg.add({user, ip, mode, now})`; on serveClient/serveControl return, `reg.remove(sid)`. `run()` owns a `SessionRegistry` and a `StatusWriter(reg, cfg.port, dir)` started after bind.
- [ ] **Step 4:** Run tests; expect PASS. Manually: start a worker, connect, confirm `status-<port>.json` lists the session.
- [ ] **Step 5: Commit** — `feat(agent): session registry + per-worker status-<port>.json`

### Task 4: Crypto/CA module (mbedTLS)

**Files:**
- Create: `admin/src/ca.h`, `admin/src/ca.cpp`
- Test: `admin/test/test_ca.cpp`

**Interfaces:**
- Produces (all PEM in/out as `std::string`, throw `CaError` on failure):
  - `struct KeyPair { std::string privPem; std::string pubPem; };`
  - `KeyPair generateRsaKey(int bits = 2048);`
  - `std::string makeSelfSignedCa(const std::string& caKeyPem, const std::string& cn, int days);`
  - `std::string makeCsr(const std::string& keyPem, const std::string& cn);`
  - `std::string issueCert(const std::string& caCertPem, const std::string& caKeyPem, const std::string& subjectKeyOrCsrPem, const std::string& cn, int days, bool serverClientEku, uint64_t serial);` — signs a leaf (from a public key or a CSR) with CA; extensions per `provision-certs.ps1` (leaf: CA:FALSE, digitalSignature+keyEncipherment, EKU serverAuth+clientAuth).
  - `std::string signCsr(const std::string& caCertPem, const std::string& caKeyPem, const std::string& csrPem, int days, uint64_t serial);`

- [ ] **Step 1: Write failing test** in `test_ca.cpp`: `TEST_CASE("issued leaf verifies against the CA")` — gen CA key, `makeSelfSignedCa`, gen leaf key, `issueCert`, then `mbedtls_x509_crt_verify` of leaf against CA returns 0 (flags 0). `TEST_CASE("CSR sign roundtrip verifies")` — gen key, `makeCsr`, `signCsr`, verify against CA.
- [ ] **Step 2:** Run; expect FAIL.
- [ ] **Step 3:** Implement with mbedTLS write APIs (`mbedtls_pk_setup`+`mbedtls_rsa_gen_key`, `mbedtls_x509write_crt_*`, `mbedtls_x509write_csr_*`, `mbedtls_x509_csr_parse`). Serial from the caller (Task 9 keeps a counter).
- [ ] **Step 4:** Run; expect PASS.
- [ ] **Step 5: Commit** — `feat(admin): mbedTLS CA — keygen, self-signed CA, issue/sign certs`

### Task 5: Password-encrypted key container

**Files:**
- Create: `admin/src/keyvault.h`, `admin/src/keyvault.cpp`
- Test: `admin/test/test_keyvault.cpp`

**Interfaces:**
- Produces:
  - `std::vector<uint8_t> sealKey(const std::string& pem, const std::string& password);` — header(magic+salt+iv+iterations) + AES-256-GCM(PBKDF2(password,salt)) ciphertext + tag. Zeroes the plaintext copy.
  - `bool openKey(const std::vector<uint8_t>& blob, const std::string& password, std::string& pemOut);` — false on wrong password / tamper (GCM tag mismatch).

- [ ] **Step 1: Write failing test** in `test_keyvault.cpp`: `TEST_CASE("seal/open roundtrip")` — seal a PEM with "pw", open with "pw" → equal. `TEST_CASE("wrong password fails cleanly")` — open with "bad" → returns false, pemOut untouched/empty.
- [ ] **Step 2:** Run; expect FAIL.
- [ ] **Step 3:** Implement with `mbedtls_pkcs5_pbkdf2_hmac_ext` (PBKDF2-SHA256, random salt, ≥200k iters) + `mbedtls_gcm_*` (AES-256-GCM, random 12-byte IV). Container header stores salt(16)+iv(12)+iters(4)+tag(16).
- [ ] **Step 4:** Run; expect PASS.
- [ ] **Step 5: Commit** — `feat(admin): password-encrypted CA key container (AES-256-GCM + PBKDF2)`

### Task 6: Config editor (preserve unknown keys)

**Files:**
- Create: `admin/src/configedit.h`, `admin/src/configedit.cpp`
- Test: `admin/test/test_configedit.cpp`

**Interfaces:**
- Produces: `class ConfigDoc { static ConfigDoc load(path); bool save(path); void setPort(uint16_t); void setListenAddress(string); void setTrayIcon(bool); void setTls(cert,key,ca); std::vector<string> allowedClients()/setAllowedClients(...); accessSids()/setAccessSids(...); accessUsers()/accessGroups()/set...; };` — wraps a `nlohmann::json` tree; setters touch only their key.

- [ ] **Step 1: Write failing test** in `test_configedit.cpp`: `TEST_CASE("editing a field preserves unknown keys")` — load JSON containing `{"port":7654,"customFutureKey":{"x":1},"access":{"sids":[]}}`, `setPort(7700)`, save to string, reparse → `port==7700` AND `customFutureKey.x==1` AND `access` intact.
- [ ] **Step 2:** Run; expect FAIL.
- [ ] **Step 3:** Implement over `nlohmann::json` (load file → json; setters assign `j["port"]=...` etc.; save `j.dump(2)`). UTF-8 for access names.
- [ ] **Step 4:** Run; expect PASS.
- [ ] **Step 5: Commit** — `feat(admin): JSON-tree config editor preserving unknown keys`

### Task 7: Agent locator + status aggregator

**Files:**
- Create: `admin/src/agentlocate.h`, `admin/src/agentlocate.cpp`
- Test: `admin/test/test_agentlocate.cpp`

**Interfaces:**
- Produces:
  - `struct AgentInstall { bool installed; std::wstring exePath; std::wstring configDir; SvcState state; std::wstring exeVersion; };`
  - `AgentInstall locateAgent();` — service `PortalVNC` ImagePath (strip args/quotes) → exe + its folder; fallback `C:\ProgramData\PortalVNC`; exe version via `GetFileVersionInfo`.
  - `struct LiveStatus { std::wstring agentVersion; std::vector<SessionInfo> sessions; std::vector<uint16_t> ports; };`
  - `LiveStatus aggregateStatus(const std::wstring& dir);` — read & merge all `status-*.json` in dir.

- [ ] **Step 1: Write failing test** in `test_agentlocate.cpp`: `TEST_CASE("aggregateStatus merges two status files")` — write two `status-7654.json`/`status-7655.json` to a temp dir (one session each), `aggregateStatus` → `sessions.size()==2`, `ports=={7654,7655}`. `TEST_CASE("locateAgent reports not installed cleanly")` — with no service, `locateAgent().installed==false` (no throw).
- [ ] **Step 2:** Run; expect FAIL.
- [ ] **Step 3:** Implement (QueryServiceConfig for ImagePath; glob `status-*.json`; nlohmann parse/merge).
- [ ] **Step 4:** Run; expect PASS.
- [ ] **Step 5: Commit** — `feat(admin): agent locator (SCM ImagePath) + status-*.json aggregator`

### Task 8: DS Object Picker wrapper

**Files:**
- Create: `admin/src/picker.h`, `admin/src/picker.cpp`

**Interfaces:**
- Produces: `struct Principal { std::wstring name; std::wstring sid; bool isGroup; };` `bool pickPrincipals(HWND owner, std::vector<Principal>& out);` — DS Object Picker limited to users+groups (local + domain), returns name + SID (SID via `LookupAccountNameW` when the picker doesn't supply it).

- [ ] **Step 1:** Implement `pickPrincipals` with `IDsObjectPicker` (`objsel.h`), scopes `DSOP_SCOPE_TYPE_TARGET_COMPUTER | UPLEVEL/DOWNLEVEL`, filter users+groups; convert the returned attributes (objectSid) to a string SID.
- [ ] **Step 2:** Manual check: the picker opens and returns name+SID (GUI/COM; no unit test).
- [ ] **Step 3: Commit** — `feat(admin): Windows object picker for users/groups -> SID`

### Task 9: Admin executable — CA role + Agent role UI

**Files:**
- Create: `admin/src/main.cpp`, `admin/src/ui_ca.cpp/.h`, `admin/src/ui_agent.cpp/.h`, `admin/src/resource.h`, `admin/src/pvnc_admin.rc`, `admin/CMakeLists.txt`
- Create: `assets/pvnc-admin.ico` (from a source png; or reuse server icon tinted) via `tools/make-icons.py`
- Modify: root `CMakeLists.txt` (`add_subdirectory(admin)`)

**Interfaces:**
- Consumes: Tasks 2,4,5,6,7,8 (service control, CA, keyvault, configedit, locator, picker).
- Produces: `pvnc-admin.exe` (GUI, WIN32_EXECUTABLE), own icon + `VS_VERSION_INFO`.

- [ ] **Step 1:** `admin/CMakeLists.txt`: `pvnc_admin` exe linking `pvnc_protocol`, `nlohmann_json`, mbedTLS, `ws2_32 advapi32 user32 gdi32 comctl32 shell32 ole32 version`; include `agent/src` for service/sessions headers (or link a small shared lib). Add the `admin/test` Catch2 target (Tasks 4–7). Wire `add_subdirectory(admin)`.
- [ ] **Step 2:** `tools/make-icons.py`: add an admin icon job → `assets/pvnc-admin.ico`. Add `pvnc_admin.rc` (icon + VERSION_INFO).
- [ ] **Step 3:** Build the UI: a start screen choosing/auto-detecting role; **CA role** — create/open CA (password prompt → keyvault), issue agent cert (A-import: gen key+issueCert → save bundle folder), sign CSR (A-CSR), issue client cert; **Agent role** — status panel (locateAgent + aggregateStatus), certificates (import bundle → copy to configDir + ConfigDoc.setTls + save), access editor (ListViews for allowedClients + access; "Add user/group" → picker → setAccessSids; IPv4 validate), service panel (install/uninstall/start/stop/restart via Task 2, edit port/listen/tray via ConfigDoc). Premium dark style consistent with the viewer.
- [ ] **Step 4:** Build; run the admin unit tests (`admin_tests.exe`) — PASS. Manual acceptance (Task 10 doc).
- [ ] **Step 5: Commit** — `feat(admin): pvnc-admin.exe — CA and agent-setup GUI`

### Task 10: Packaging, docs, acceptance

**Files:**
- Create: `admin/README.md`, `docs/superpowers/plans/agent-manager-acceptance.md`
- Modify: `README.md` (mention the manager), `.github/workflows/release.yml` (ship `pvnc-admin.exe` in the zip)

- [ ] **Step 1:** Add `pvnc-admin.exe` to the release zip in the workflow; README section for the manager.
- [ ] **Step 2:** Manual acceptance checklist: create CA, issue agent+client bundles, import on a machine, edit access via picker, install+start service, status shows version+sessions, viewer connects with the issued client cert end-to-end. Record results.
- [ ] **Step 3: Commit** — `docs/ci: ship and document pvnc-admin`

---

## Self-review

- **Spec coverage:** CA (T4,T5,T9), agent cert import/CSR (T4,T9), client cert (T4,T9), status (T3,T7,T9), access+picker (T6,T8,T9), service control (T2,T9), version (T1), packaging standalone (T9,T10). Covered.
- **Review Focus:** wrong password (T5), unknown-key preservation (T6), not-installed (T7), two sessions aggregation (T7), issued cert verifies (T4 + manual). Each has a test or explicit manual acceptance.
- **Types consistent:** `SvcState`, `SessionInfo`, `ConfigDoc`, `AgentInstall`, `KeyPair` defined once and consumed by name.
- **GUI/COM/SCM-install** steps are manual by nature (OS/elevation); unit tests cover the pure logic (crypto, container, config editor, status aggregation, query-state).
- **Proportion:** plan adds the signatures/tests/decisions the design leaves open; bodies only where an algorithm isn't determined.
