'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const telemetry = require('../src/telemetry');

const ID = '0b6f7c1e-5d1a-4c3e-9f2a-6a8b0c9d1e2f';

function harness({ sharing = true, respond = () => ({ ok: true, json: async () => ({ online: 3, playing: 1 }) }) } = {}) {
  const requests = [], counts = [], timers = [];
  const state = { sharing };
  const reporter = telemetry.createReporter({
    url: 'https://api.test', sharing: () => state.sharing,
    fetch: async (url, options = {}) => { requests.push({ url, ...options }); return respond(); },
    getPayload: () => telemetry.payload({ installId: ID, launcherVersion: '0.1.51', channel: 'stable' }, 'linux', 'x64'),
    onCount: count => counts.push(count),
    setInterval: (callback, ms) => { timers.push({ callback, ms }); return timers.length; },
    clearInterval: id => { timers[id - 1].cleared = true; }
  });
  return { reporter, requests, counts, timers, state };
}

test('the endpoint is empty until deployed and can be pointed at a test API', () => {
  assert.equal(telemetry.endpoint({}), telemetry.TELEMETRY_URL.replace(/\/+$/, ''));
  assert.equal(telemetry.endpoint({ SMS_TELEMETRY_URL: 'http://localhost:8787/' }), 'http://localhost:8787');
});

test('install IDs must be UUIDs', () => {
  assert.equal(telemetry.validInstallId(ID), true);
  for (const value of [undefined, '', 'abc', 42, `${ID}x`]) assert.equal(telemetry.validInstallId(value), false);
});

test('a heartbeat holds only versions, OS, channel, playing and the install ID', () => {
  assert.deepEqual(telemetry.payload({ installId: ID, launcherVersion: '0.1.51', gameVersion: '2026.10.07.3',
    availableVersion: '2026.10.07.3', channel: 'beta', playing: 1, rom: '/secret/disc.iso' }, 'win32', 'x64'), {
    installId: ID, launcherVersion: '0.1.51', gameVersion: '2026.10.07.3', availableVersion: '2026.10.07.3',
    platform: 'win32', arch: 'x64', channel: 'beta', playing: true
  });
  assert.equal(telemetry.payload({ installId: ID }).gameVersion, null);
});

test('online counts from the API are checked before use', () => {
  assert.deepEqual(telemetry.onlineCount({ online: 5, playing: 2 }), { online: 5, playing: 2 });
  assert.deepEqual(telemetry.onlineCount({ online: 5 }), { online: 5, playing: 0 });
  for (const body of [null, {}, { online: -1 }, { online: '5' }, { online: 1.5 }]) assert.equal(telemetry.onlineCount(body), null);
});

test('sharing sends a heartbeat right away, then every interval, and reports the count', async () => {
  const { reporter, requests, counts, timers } = harness();
  await reporter.start();
  assert.equal(requests.length, 1);
  assert.equal(requests[0].url, 'https://api.test/v1/heartbeat');
  assert.equal(requests[0].method, 'POST');
  assert.equal(JSON.parse(requests[0].body).installId, ID);
  assert.deepEqual(counts, [{ online: 3, playing: 1 }]);
  assert.equal(timers.length, 1);
  assert.equal(timers[0].ms, telemetry.INTERVAL);
  await reporter.start();
  assert.equal(timers.length, 1, 'starting twice keeps one timer');
  await timers[0].callback();
  assert.equal(requests.length, 3);
  reporter.stop();
  assert.equal(timers[0].cleared, true);
});

test('with sharing off only the public count is asked for', async () => {
  const { reporter, requests, counts, state } = harness({ sharing: false });
  await reporter.start();
  assert.deepEqual(requests.map(request => [request.url, request.method, request.body]), [['https://api.test/v1/online', undefined, undefined]]);
  assert.deepEqual(counts, [{ online: 3, playing: 1 }]);
  state.sharing = true;
  await reporter.tick();
  assert.equal(requests[1].url, 'https://api.test/v1/heartbeat');
});

test('offline or failed requests are silent and keep the last count', async () => {
  for (const respond of [() => { throw new Error('offline'); }, () => ({ ok: false, json: async () => ({ online: 9 }) }),
    () => ({ ok: true, json: async () => { throw new SyntaxError('bad'); } }), () => ({ ok: true, json: async () => ({}) })]) {
    const { reporter, counts } = harness({ respond });
    await reporter.start();
    assert.deepEqual(counts, []);
  }
});
