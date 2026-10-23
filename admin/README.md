# PortalVNC Agent Manager (`pvnc-admin.exe`)

A standalone native Windows GUI for running a small certificate authority and
configuring the PortalVNC agent on each machine. No .NET, no runtime — one
portable executable.

It has two roles, shown as tabs.

## Certificate Authority

Run this on the **administrator's PC** (model A: one central CA).

- **Create CA** — pick a folder, enter a Common Name and a password. The tool
  generates a 4096-bit RSA CA, writes the self-signed `ca-cert.pem`, and stores
  the CA private key in an encrypted container `ca-key.vault`
  (AES-256-GCM, key derived with PBKDF2-HMAC-SHA256, 200k iterations).
  **The CA private key is never written unencrypted to disk and never leaves
  this machine.**
- **Open CA** — pick the CA folder and enter the password to unlock it for the
  session (the key lives only in memory while the app is open).
- **Issue agent bundle** / **Issue client bundle** — generates a fresh 2048-bit
  key pair and a leaf certificate (serverAuth + clientAuth EKU) signed by the
  CA, and writes a bundle folder containing `ca.pem`, `*-cert.pem`, `*-key.pem`.
- **Sign CSR** — sign a certificate request produced elsewhere, so a client
  private key need never leave the client machine.

Serial numbers are tracked per CA in `ca-serial.txt`.

## Agent

Run this on a **machine running the agent** (locally or over a remote session).

- **Status** — whether the `PortalVNC` service is installed, its exe path and
  file version, the service state, and the config directory. **Active sessions**
  are read live from the per-worker `status-<port>.json` files the agent writes.
- **Service** — install, uninstall, start, stop, restart the service.
  Installing/uninstalling and writing the config under `C:\ProgramData` need the
  manager to run **as administrator**.
- **Certificates** — import a bundle folder: the tool copies `ca.pem`,
  `agent-cert.pem`, `agent-key.pem` (or `client-*`) into the config directory
  and points `config.json` → `tls` at them.
- **Access & network** — edit the allowed client IPs/CIDRs and the access lists.
  **Add user/group** opens the Windows object picker (local + domain) and stores
  the choice as a SID in `access.sids`. Port, listen address and the tray-icon
  flag are edited here too. Unknown/future keys in `config.json` are preserved.

## Build

Built as part of the top-level CMake project (`x64-windows-static`, mbedTLS +
Win32). The executable is `build/admin/Release/pvnc-admin.exe` and ships in the
release zip alongside the agent and viewer.

Unit tests (crypto, key container, config editor, status aggregation) run via
`build/admin/Release/pvnc_admin_tests.exe`.
