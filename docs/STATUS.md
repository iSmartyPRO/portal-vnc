# PortalVNC — implementation status

All five planned milestones are implemented and committed.

## What is done and how it was verified

| Plan | Component | Verification |
|---|---|---|
| 1 | Protocol (mTLS, handshake, versioning, logon + SID) | ✅ 40 automated tests + live reject paths |
| 1 | Agent: config, IP filter, capture, RFB, input | ✅ 72 automated tests (incl. an RFB-over-mTLS integration test) |
| 1 | Viewer (Win32 window) | ✅ live logon with a real password, streaming |
| 2 | Windows service + tray icon | ✅ build + worker mode; full service via MSI / elevated run |
| 2 | Lock screen / UAC (input-desktop following) | ✅ code; full check on a real locked session |
| 3 | Multi-session / terminal server | ✅ `--port`, per-session supervisor; RDP on a real TS |
| 4 | Browser↔agent gateway + noVNC | ✅ **live: desktop rendered in the browser** |
| 5 | MSI installer (service + firewall) | ✅ builds and validates; install needs elevation / GPO |

## Verified live on a dev machine

- The native viewer connects over mTLS, authenticates, and views/controls the desktop.
- Wrong password → rejected; rogue certificate → rejected; off-allowlist address → rejected.
- Worker (service) mode streams with input-desktop following.
- `--port` override.
- **Browser (noVNC) → gateway → agent: a real Windows desktop in the browser.**

## Needs validation in a real environment (not reproducible headless / locally)

- The full service lifecycle (install→start→stop) and **lock-screen / UAC** capture —
  requires an elevated run and a real locked session.
- Multiple **RDP sessions** on a terminal server.
- **MSI** deployment via Group Policy in a domain.
- Deploying the gateway and the noVNC viewer **into a running web portal** (frontend
  build, nginx, container restart) — code and instructions are in `gateway/README.md`.
- Provisioning **production certificates** (a fleet-wide agent certificate) and config via GPO.

## Next steps for production

1. Issue a production CA + agent certificate + portal client certificate; distribute via GPO.
2. Install the MSI on a test OU; verify lock-screen / UAC and the log.
3. Wire the gateway into the portal's `remote` module (see `gateway/README.md`), add the
   `portalvnc` host type.
4. Validate multi-session behavior on a terminal server.
