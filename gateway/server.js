'use strict';
// PortalVNC gateway: a WebSocket server that bridges a browser (noVNC) to an
// agent. The browser connects with a one-time token; the gateway looks up the
// target host + Windows credentials for that token (never sent to the browser),
// opens a mutual-TLS PortalVNC connection to the agent, and then relays raw RFB
// bytes in both directions.
//
// In the portal this role is filled by the portal's own module, which mints the
// token and holds the credential vault. This standalone server lets the gateway
// run and be tested on its own; config.json maps tokens -> targets.
const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');
const { connectAgent } = require('./pvnc');

const CONFIG = process.env.PVNC_GW_CONFIG || path.join(__dirname, 'config.json');
const cfg = JSON.parse(fs.readFileSync(CONFIG, 'utf8'));
const PORT = cfg.listenPort || 8787;
const sessions = cfg.sessions || {}; // token -> { host, port, caPath, certPath, keyPath, username, domain, password }

// Serve the static noVNC test page from ./public.
const server = http.createServer((req, res) => {
  let p = req.url.split('?')[0];
  if (p === '/') p = '/index.html';
  const file = path.join(__dirname, 'public', path.normalize(p).replace(/^(\.\.[/\\])+/, ''));
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end('not found'); return; }
    const ext = path.extname(file);
    const type = ext === '.html' ? 'text/html' : ext === '.js' ? 'text/javascript' : 'application/octet-stream';
    res.writeHead(200, { 'content-type': type });
    res.end(data);
  });
});

const wss = new WebSocketServer({ server, path: '/ws' });

wss.on('connection', async (ws, req) => {
  const url = new URL(req.url, 'http://localhost');
  const token = url.searchParams.get('token');
  const target = sessions[token];
  if (!target) { ws.close(1008, 'invalid token'); return; }

  let agent;
  try {
    agent = await connectAgent(target);
  } catch (e) {
    console.error('[gw] agent connect failed:', e.code || e.message);
    try { ws.close(1011, String(e.code || e.message)); } catch (_) {}
    return;
  }
  console.log('[gw] bridged token=%s -> %s:%s', token.slice(0, 6), target.host, target.port);

  // Relay: browser WS (binary RFB) <-> agent TLS (raw RFB).
  ws.on('message', (data) => { if (agent.writable) agent.write(data); });
  agent.on('data', (chunk) => { if (ws.readyState === ws.OPEN) ws.send(chunk); });

  const shutdown = () => { try { agent.destroy(); } catch (_) {} try { ws.close(); } catch (_) {} };
  ws.on('close', shutdown);
  ws.on('error', shutdown);
  agent.on('close', shutdown);
  agent.on('error', shutdown);
});

server.listen(PORT, () => {
  console.log('PortalVNC gateway on http://localhost:%d (ws %s/ws)', PORT, PORT);
});
