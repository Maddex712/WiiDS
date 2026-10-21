'use strict';

// Discord Rich Presence through the Discord app's local socket (a named pipe
// on Windows): a handshake with our application ID, then SET_ACTIVITY frames.
// Discord drops the activity when the socket closes, so stop() only closes it.
// Without Discord running nothing happens; it is looked for again now and then.
const fs = require('node:fs');
const net = require('node:net');
const path = require('node:path');
const crypto = require('node:crypto');

const CLIENT_ID = '1557924964869480578';
const OP = { HANDSHAKE: 0, FRAME: 1, CLOSE: 2, PING: 3, PONG: 4 };
// Discord accepts five activity updates per 20 seconds.
const MIN_INTERVAL = 4000;
const RETRY = 20000;

function encode(op, data) {
  const body = Buffer.from(JSON.stringify(data));
  const header = Buffer.alloc(8);
  header.writeUInt32LE(op, 0);
  header.writeUInt32LE(body.length, 4);
  return Buffer.concat([header, body]);
}

// Socket bytes -> whole frames, keeping a partial frame for the next chunk.
function createDecoder(onFrame) {
  let buffer = Buffer.alloc(0);
  return chunk => {
    buffer = Buffer.concat([buffer, chunk]);
    while (buffer.length >= 8 && buffer.length >= 8 + buffer.readUInt32LE(4)) {
      const op = buffer.readUInt32LE(0), end = 8 + buffer.readUInt32LE(4);
      let data = null;
      try { data = JSON.parse(buffer.toString('utf8', 8, end)); } catch (_) { /* not JSON */ }
      buffer = buffer.subarray(end);
      onFrame(op, data);
    }
  };
}

// Where Discord listens, most likely first: discord-ipc-0 in each folder before -1.
// Flatpak and Snap Discord, and Vesktop's Flatpak, put it in a subfolder.
function socketPaths(platform = process.platform, env = process.env) {
  if (platform === 'win32') return Array.from({ length: 10 }, (_, i) => `\\\\?\\pipe\\discord-ipc-${i}`);
  const bases = [...new Set([env.XDG_RUNTIME_DIR, env.TMPDIR, env.TMP, env.TEMP, '/tmp'].filter(Boolean))];
  const folders = ['', 'app/com.discordapp.Discord', 'app/com.discordapp.DiscordCanary', 'snap.discord',
    'snap.discord-canary', '.flatpak/dev.vencord.Vesktop/xdg-run'];
  const paths = [];
  for (let i = 0; i < 10; ++i)
    for (const base of bases) for (const folder of folders) paths.push(path.posix.join(base, folder, `discord-ipc-${i}`));
  return paths;
}

function createPresence({ clientId = CLIENT_ID, platform = process.platform, paths = socketPaths(platform),
  exists = platform === 'win32' ? () => true : fs.existsSync, connect = target => net.createConnection(target),
  pid = process.pid, now = Date.now, setTimeout = global.setTimeout, clearTimeout = global.clearTimeout,
  log = () => {} } = {}) {
  let socket = null, ready = false, connecting = false, timer = null;
  let wanted = null, sent = null, sentAt = -Infinity;

  function later(callback, ms) {
    if (timer) return;
    timer = setTimeout(() => { timer = null; callback(); }, ms);
  }

  function update() {
    if (!wanted) return;
    if (!socket) { if (!connecting) open(); return; }
    if (!ready) return;
    const body = JSON.stringify(wanted);
    if (body === sent) return;
    const wait = sentAt + MIN_INTERVAL - now();
    if (wait > 0) return later(update, wait);
    sent = body;
    sentAt = now();
    socket.write(encode(OP.FRAME, { cmd: 'SET_ACTIVITY', args: { pid, activity: wanted }, nonce: crypto.randomUUID() }));
  }

  function open() {
    connecting = true;
    const candidates = paths.filter(target => exists(target));
    const attempt = index => {
      if (!wanted) { connecting = false; return; }
      if (index >= candidates.length) { connecting = false; return later(update, RETRY); }
      const candidate = connect(candidates[index]);
      candidate.once('error', () => { candidate.destroy(); attempt(index + 1); });
      candidate.once('connect', () => {
        candidate.removeAllListeners('error');
        connecting = false;
        if (!wanted) return candidate.destroy();
        attach(candidate);
      });
    };
    attempt(0);
  }

  function attach(candidate) {
    socket = candidate;
    candidate.on('error', () => {});
    candidate.on('data', createDecoder((op, data) => {
      if (op === OP.PING) candidate.write(encode(OP.PONG, data));
      else if (op === OP.CLOSE) {
        log(`Discord closed the connection: ${data?.message || 'no reason given'}`);
        candidate.destroy();
      } else if (data?.evt === 'READY') { ready = true; update(); }
      else if (data?.evt === 'ERROR') log(`Discord did not accept the activity: ${data.data?.message || 'unknown error'}`);
    }));
    candidate.on('close', () => {
      if (socket !== candidate) return;
      socket = null; ready = false; sent = null;
      if (wanted) later(update, RETRY);
    });
    candidate.write(encode(OP.HANDSHAKE, { v: 1, client_id: clientId }));
  }

  return {
    set(activity) { wanted = activity; update(); },
    stop() {
      wanted = null; sent = null;
      if (timer) { clearTimeout(timer); timer = null; }
      if (socket) { const closing = socket; socket = null; ready = false; closing.destroy(); }
    }
  };
}

module.exports = { CLIENT_ID, OP, MIN_INTERVAL, RETRY, encode, createDecoder, socketPaths, createPresence };
