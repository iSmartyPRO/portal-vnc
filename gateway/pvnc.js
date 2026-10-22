'use strict';
// PortalVNC client handshake for the gateway. Opens a mutual-TLS connection to
// an agent, performs our version/credential handshake, and resolves with a
// raw (post-auth) TLS socket over which standard RFB then flows. Mirrors
// protocol/src/{protocol,handshake}.cpp.
const tls = require('tls');
const fs = require('fs');

const MAGIC = 0x50564e31; // "PVN1"
const VERSION = 1;

const REFUSE = {
  0: 'none',
  1: 'protocol version mismatch',
  2: 'authentication failed (bad credentials)',
  3: 'access denied: not a member of Administrators',
  4: 'not allowed (TLS/cert or IP)',
  5: 'bad handshake',
};

function u16(n) { const b = Buffer.alloc(2); b.writeUInt16BE(n & 0xffff); return b; }
function field(str) {
  const s = Buffer.from(str || '', 'utf8');
  return Buffer.concat([u16(s.length), s]);
}

// Reads exactly n bytes from a socket (async).
function readN(sock, n) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let have = 0;
    const onData = (d) => {
      chunks.push(d);
      have += d.length;
      if (have >= n) {
        sock.removeListener('data', onData);
        sock.removeListener('error', onErr);
        sock.removeListener('close', onClose);
        const all = Buffer.concat(chunks);
        // Put back any extra bytes for the next reader.
        if (all.length > n) sock.unshift(all.subarray(n));
        resolve(all.subarray(0, n));
      }
    };
    const onErr = (e) => { cleanup(); reject(e); };
    const onClose = () => { cleanup(); reject(new Error('closed during handshake')); };
    const cleanup = () => {
      sock.removeListener('data', onData);
      sock.removeListener('error', onErr);
      sock.removeListener('close', onClose);
    };
    sock.on('data', onData);
    sock.on('error', onErr);
    sock.on('close', onClose);
  });
}

// opts: { host, port, caPath, certPath, keyPath, username, domain, password }
// Resolves to a connected tls socket positioned right before RFB. Rejects with
// an Error whose .code is the refuse string.
function connectAgent(opts) {
  return new Promise((resolve, reject) => {
    const socket = tls.connect({
      host: opts.host,
      port: opts.port,
      ca: [fs.readFileSync(opts.caPath)],
      cert: fs.readFileSync(opts.certPath),
      key: fs.readFileSync(opts.keyPath),
      minVersion: 'TLSv1.2',
      maxVersion: 'TLSv1.2',
      rejectUnauthorized: true,
      // We authenticate the agent by CA trust, not hostname.
      checkServerIdentity: () => undefined,
    }, async () => {
      try {
        // ClientHello: magic(4) version(2) caps(4)
        const hello = Buffer.alloc(10);
        hello.writeUInt32BE(MAGIC, 0);
        hello.writeUInt16BE(VERSION, 4);
        hello.writeUInt32BE(0, 6);
        socket.write(hello);

        // ServerHello: magic(4) version(2) caps(4) refuse(2)
        const sh = await readN(socket, 12);
        if (sh.readUInt32BE(0) !== MAGIC) throw refuseErr(5);
        const shRefuse = sh.readUInt16BE(10);
        if (shRefuse !== 0) throw refuseErr(shRefuse);

        // Credentials: domain, user, password (each u16 len + utf8)
        socket.write(Buffer.concat([
          field(opts.domain), field(opts.username), field(opts.password),
        ]));

        // Auth result: refuse(2)
        const ar = await readN(socket, 2);
        const authRefuse = ar.readUInt16BE(0);
        if (authRefuse !== 0) throw refuseErr(authRefuse);

        resolve(socket); // RFB flows from here
      } catch (e) {
        socket.destroy();
        reject(e);
      }
    });
    socket.on('error', (e) => reject(e));
  });
}

function refuseErr(code) {
  const e = new Error('PortalVNC refused: ' + (REFUSE[code] || code));
  e.code = REFUSE[code] || String(code);
  e.refuse = code;
  return e;
}

module.exports = { connectAgent, REFUSE };
