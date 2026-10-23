# PortalVNC gateway (browser ↔ agent)

A bridge between the browser (noVNC) and a PortalVNC agent. It terminates the
browser's WebSocket, opens a mutual-TLS connection to the agent, performs our
handshake with the Windows credentials, and then **relays raw RFB** in both
directions. Standard noVNC runs in the browser — the agent speaks ordinary RFB,
so nothing special is needed on the browser side.

```
Browser (noVNC, RFB-over-WS) ──ws──► Gateway ──mTLS + PortalVNC handshake──► Agent
```

## Files

| File | Purpose |
|---|---|
| `pvnc.js` | PortalVNC client: mTLS to the agent + handshake (version, credentials, authorization). `connectAgent(opts)` → a TLS socket ready for RFB. Reusable by the portal. |
| `server.js` | Standalone WebSocket server: maps a token to a target + credentials, calls `connectAgent`, then relays RFB (with backpressure). For development and testing. |
| `public/index.html` | noVNC test page. |
| `config.example.json` | Example: token → target + credentials. Never commit real passwords. |

## Local run and test

```bash
# 1) agent on 127.0.0.1:7654 (dev)
build/agent/Release/pvnc-agent.exe --worker --config certs/agent-config.json
# 2) gateway (config maps a token -> target + credentials)
PVNC_GW_CONFIG=certs/gw-config.json node gateway/server.js
# 3) open http://localhost:8787/ , token "dev", click Connect
```

Verified: a real Windows desktop renders in the browser through this path.

## Integrating with the web portal (docker-app-skeleton, `remote` module)

The portal already plays the role of "token minter and credential vault" for guacd.
PortalVNC connects **bypassing guacd**, with its own bridge. Minimal changes:

1. **Backend.** Add a WS route (e.g. `/api/app/remote/pvnc`) next to the existing guacd
   tunnel. In the handler:
   - verify the user's session and permission (as the current `remote` module does);
   - take `{host, port, username, domain, password}` from the portal's credential vault
     (`resolveCredentials`, as for RDP) plus the PortalVNC CA/client-certificate paths
     from the module settings;
   - call `connectAgent(...)` from `gateway/pvnc.js` and relay bytes between the browser's
     WebSocket and the returned socket (see `server.js`).
   Credentials never reach the browser — everything stays in the backend, as with guacd.
2. **Nginx.** Give the new route the same upgrade headers and long timeout as
   `/api/app/remote/tunnel`.
3. **Frontend.** In `RemoteViewer.jsx`, for hosts of type `portalvnc`, open a noVNC `RFB`
   on the new WS route instead of guacamole-common-js. The rest of the UI (catalog, log,
   credential vault) stays the same.
4. **Catalog.** Add a `portalvnc` host type (`protocol` field); routing to the PortalVNC
   gateway is chosen by type, transparently to the user.

Deploying into a running portal (frontend build, container restart, nginx) is a separate
step on the portal side; this repository ships the reusable `pvnc.js` and the reference
`server.js`.

## Security

- The password and address never reach the browser — only a token does.
- The gateway holds a client certificate from your CA: that is what makes the portal a
  trusted client to the agent. A WS client without a valid token is rejected; an agent
  will not accept anyone without your certificate.
