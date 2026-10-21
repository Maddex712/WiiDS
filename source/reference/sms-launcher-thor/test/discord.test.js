'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const discord = require('../src/discord');

test('frames survive being split across chunks and joined in one', () => {
  const frames = [];
  const decode = discord.createDecoder((op, data) => frames.push([op, data]));
  const bytes = Buffer.concat([discord.encode(1, { evt: 'READY' }), discord.encode(3, { n: 1 })]);
  decode(bytes.subarray(0, 5));
  decode(bytes.subarray(5, 20));
  decode(bytes.subarray(20));
  assert.deepEqual(frames, [[1, { evt: 'READY' }], [3, { n: 1 }]]);
});

test('Discord is looked for in its usual places, discord-ipc-0 first', () => {
  assert.equal(discord.socketPaths('win32')[0], '\\\\?\\pipe\\discord-ipc-0');
  const paths = discord.socketPaths('linux', { XDG_RUNTIME_DIR: '/run/user/1000' });
  assert.equal(paths[0], '/run/user/1000/discord-ipc-0');
  assert.ok(paths.includes('/run/user/1000/app/com.discordapp.Discord/discord-ipc-0'));
  assert.ok(paths.includes('/tmp/snap.discord/discord-ipc-0'));
  assert.ok(paths.indexOf('/tmp/discord-ipc-0') < paths.indexOf('/run/user/1000/discord-ipc-1'));
});

// A stand-in for the Discord app on a local socket.
function fakeDiscord(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-discord-'));
  // Windows can only listen on a named pipe, which is also where Discord is found there.
  const file = process.platform === 'win32' ? `\\\\?\\pipe\\${path.basename(dir)}-discord-ipc-0`
    : path.join(dir, 'discord-ipc-0');
  const received = [], waiters = [];
  let client = null;
  const server = net.createServer(socket => {
    client = socket;
    socket.on('data', discord.createDecoder((op, data) => {
      received.push({ op, data });
      if (op === discord.OP.HANDSHAKE) socket.write(discord.encode(discord.OP.FRAME, { cmd: 'DISPATCH', evt: 'READY' }));
      waiters.splice(0).forEach(resolve => resolve());
    }));
  });
  t.after(() => { client?.destroy(); server.close(); fs.rmSync(dir, { recursive: true, force: true }); });
  const next = count => received.length >= count ? Promise.resolve()
    : new Promise(resolve => waiters.push(resolve)).then(() => next(count));
  return new Promise(resolve => server.listen(file, () => resolve({ file, received, next, closed: () => new Promise(done => {
    if (!client || client.destroyed) return done();
    client.once('close', done);
  }) })));
}

test('an activity is sent after the handshake, and repeats and bursts are held back', async t => {
  const fake = await fakeDiscord(t);
  let clock = 1000;
  const timers = [];
  const presence = discord.createPresence({ clientId: '42', platform: process.platform, paths: [fake.file], pid: 7,
    now: () => clock, setTimeout: (callback, ms) => { timers.push({ callback, ms }); return timers.length; },
    clearTimeout: () => {} });
  presence.set({ details: 'Bianco Hills' });
  await fake.next(2);
  assert.deepEqual(fake.received[0], { op: discord.OP.HANDSHAKE, data: { v: 1, client_id: '42' } });
  assert.equal(fake.received[1].data.cmd, 'SET_ACTIVITY');
  assert.deepEqual(fake.received[1].data.args, { pid: 7, activity: { details: 'Bianco Hills' } });

  presence.set({ details: 'Bianco Hills' });
  presence.set({ details: 'Ricco Harbor' });
  assert.equal(timers.length, 1, 'the second change waits for the rate limit');
  assert.equal(timers[0].ms, discord.MIN_INTERVAL);
  clock += discord.MIN_INTERVAL;
  timers[0].callback();
  await fake.next(3);
  assert.deepEqual(fake.received[2].data.args.activity, { details: 'Ricco Harbor' });

  presence.stop();
  await fake.closed();
});

test('without Discord nothing connects, and it is looked for again later', () => {
  const timers = [], connects = [];
  const presence = discord.createPresence({ platform: 'linux', paths: ['/nonexistent/discord-ipc-0'],
    connect: target => { connects.push(target); throw new Error('not expected'); },
    setTimeout: (callback, ms) => { timers.push({ callback, ms }); return timers.length; }, clearTimeout: () => {} });
  presence.set({ details: 'Delfino Plaza' });
  assert.deepEqual(connects, []);
  assert.deepEqual(timers.map(timer => timer.ms), [discord.RETRY]);
  presence.stop();
});
