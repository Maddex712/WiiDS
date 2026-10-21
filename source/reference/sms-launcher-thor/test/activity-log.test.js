'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { createActivityLog, formatEntry } = require('../src/activity-log');

test('snapshot replay cannot duplicate live output or replace newer partial output', () => {
  const backend = createActivityLog();
  const first = backend.write('Starting', 'launcher');
  const partial = backend.write('Caf', 'stdout');
  const snapshot = backend.snapshot();
  const warning = backend.write('warning', 'stderr');
  const complete = backend.write('Café', 'stdout', partial);
  const renderer = createActivityLog();
  renderer.merge(complete);
  renderer.merge(warning);
  for (const entry of snapshot) renderer.merge(entry);
  renderer.merge(first);
  assert.deepEqual(renderer.snapshot(), backend.snapshot());
  assert.deepEqual(renderer.snapshot().map(formatEntry), ['[launcher] Starting', '[stdout] Café', '[stderr] warning']);
});

test('backend and renderer retain the same complete entries when history is bounded', () => {
  const backend = createActivityLog(3), renderer = createActivityLog(3);
  const old = backend.write('old', 'stdout');
  for (let i = 0; i < 5; ++i) renderer.merge(backend.write(String(i), 'stderr'));
  renderer.merge(old);
  for (const entry of backend.snapshot()) renderer.merge(entry);
  assert.deepEqual(renderer.snapshot(), backend.snapshot());
  assert.deepEqual(renderer.snapshot().map(entry => entry.text), ['2', '3', '4']);
});

test('reset rejects old snapshots without discarding newer live output or reusing IDs', () => {
  const backend = createActivityLog(), renderer = createActivityLog();
  const old = backend.write('old run');
  renderer.merge(old);
  backend.reset();
  const checkpoint = backend.resetSequence();
  const fresh = backend.write('new run');
  renderer.merge(fresh);
  renderer.reset(checkpoint);
  renderer.merge(old);
  renderer.reset(checkpoint);
  assert.deepEqual(renderer.snapshot(), [fresh]);
  assert.ok(fresh.id > old.id);
});
