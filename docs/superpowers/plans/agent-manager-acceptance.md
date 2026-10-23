# Agent Manager — Manual Acceptance Checklist

The pure logic (CA issuance, key container, config editor, status aggregation,
service query) is covered by `pvnc_admin_tests` and `pvnc_agent_tests`. The
items below are the GUI/COM/SCM/end-to-end steps that need a human and a real
machine. Record pass/fail and notes.

## Build
- [ ] `cmake --build --preset default` produces `pvnc-admin.exe`, `pvnc-agent.exe`, `pvnc-viewer.exe`.
- [ ] `pvnc_admin_tests.exe` and `pvnc_agent_tests.exe` pass.
- [ ] `pvnc-admin.exe` reports the right FileVersion (matches `VERSION`).

## CA role (admin PC)
- [ ] Create CA → folder gets `ca-cert.pem`, `ca-key.vault`, `ca-serial.txt`; no plaintext key on disk.
- [ ] Reopen the app, Open CA with the right password → unlocks; wrong password → clean "wrong password" message, no crash.
- [ ] Issue agent bundle → folder has `ca.pem` + `agent-cert.pem` + `agent-key.pem`; serial advanced.
- [ ] Issue client bundle → `client-cert.pem` + `client-key.pem`.
- [ ] Sign CSR (make one with `pvnc-admin`-issued key or openssl) → signed cert verifies against `ca.pem`.

## Agent role (agent machine)
- [ ] Status shows installed=yes, exe path, version, service state when the service exists; installed=no without it (no crash).
- [ ] With a running agent and an active viewer connection, Active sessions lists the session (user, address, mode, since).
- [ ] Import bundle → `config.json` `tls` points at the copied `ca.pem`/`agent-cert.pem`/`agent-key.pem`.
- [ ] Add IP (valid) accepted; invalid IP rejected. Remove works.
- [ ] Add user/group opens the object picker; chosen principal appears as a SID row; Save writes it to `access.sids`.
- [ ] Edit port/listen/tray + Save → `config.json` updated; a pre-existing unknown key is still present after save.
- [ ] Install / Start / Stop / Restart / Uninstall work when run as administrator; a clear message when not elevated.

## End-to-end
- [ ] On the agent machine, import an agent bundle, set an allowed IP + access SID, start the service.
- [ ] From the viewer machine, connect using the matching **client** bundle (client cert signed by the same CA).
- [ ] The viewer authenticates (Windows login of an allowed account) and streams the desktop.
- [ ] A stock VNC client (no client cert) is refused at the TLS layer.
