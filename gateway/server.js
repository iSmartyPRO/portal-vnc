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

// Serve the static noVNC test page from ./public (confined to that directory).
const PUBLIC_DIR = path.resolve(__dirname, 'public');
const server = http.createServer((req, res) => {
  let p = decodeURIComponent(req.url.split('?')[0]);
  if (p === '/') p = '/index.html';
  const file = path.resolve(PUBLIC_DIR, '.' + path.posix.normalize(p));
  if (file !== PUBLIC_DIR && !file.startsWith(PUBLIC_DIR + path.sep)) {
    res.writeHead(403); res.end('forbidden'); return;
  }
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

  // Backpressure: if the browser can't keep up, pause reading from the agent
  // so a fast desktop over a slow link doesn't grow the send buffer unbounded.
  const HIGH = 8 * 1024 * 1024, LOW = 1 * 1024 * 1024;
  let drainTimer = null;
  agent.on('data', (chunk) => {
    if (ws.readyState !== ws.OPEN) return;
    ws.send(chunk);
    if (ws.bufferedAmount > HIGH && !drainTimer) {
      agent.pause();
      drainTimer = setInterval(() => {
        if (ws.readyState !== ws.OPEN || ws.bufferedAmount < LOW) {
          clearInterval(drainTimer); drainTimer = null; agent.resume();
        }
      }, 50);
    }
  });

  const shutdown = () => {
    if (drainTimer) { clearInterval(drainTimer); drainTimer = null; }
    try { agent.destroy(); } catch (_) {}
    try { ws.close(); } catch (_) {}
  };
  ws.on('close', shutdown);
  ws.on('error', shutdown);
  agent.on('close', shutdown);
  agent.on('error', shutdown);
});

server.listen(PORT, () => {
  console.log('PortalVNC gateway on http://localhost:%d (ws %s/ws)', PORT, PORT);
});
