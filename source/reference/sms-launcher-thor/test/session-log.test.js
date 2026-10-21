'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { createSessionLog } = require('../src/session-log');

test('saved logs retain early diagnostics beyond the activity limit and include live partial output', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-session-log-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const journal = createSessionLog(() => root, error => { throw error; });
  journal.write('[port] fatal signal SIGSEGV (11) at address 0x1', 'stderr');
  for (let i = 0; i < 1000; ++i) journal.write(`stub ${i}`, 'stderr');
  journal.write('', 'stdout');
  const destination = path.join(root, 'saved.log');
  journal.save(destination, [{ stream: 'stdout', text: 'live prompt' }]);
  const text = fs.readFileSync(destination, 'utf8');
  assert.match(text, /^\[stderr\] \[port\] fatal signal SIGSEGV/);
  assert.match(text, /stub 999\n\[stdout\] \n\[stdout\] live prompt\n$/);
  assert.equal(text.split('\n').length, 1004);
  assert.equal(journal.read([{ stream: 'stdout', text: 'live prompt' }]), text);
});

test('a disk logging failure is reported once without interrupting process output', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-session-log-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.writeFileSync(path.join(root, 'logs'), 'not a directory');
  const failures = [];
  const journal = createSessionLog(() => root, error => failures.push(error));
  journal.write('error one', 'stderr');
  journal.write('error two', 'stderr');
  assert.equal(failures.length, 1);
  assert.throws(() => journal.save(path.join(root, 'saved.log')), /could not be saved/);
  assert.throws(() => journal.read(), /could not be copied/);
});

test('starting a fresh log replaces the previous contents without adding another file', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-session-reset-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const journal = createSessionLog(() => root, error => { throw error; });
  journal.write('old crash', 'stderr');
  const files = fs.readdirSync(path.join(root, 'logs'));
  journal.reset();
  assert.equal(journal.read(), '');
  journal.write('new run', 'stdout');
  assert.equal(journal.read(), '[stdout] new run\n');
  assert.deepEqual(fs.readdirSync(path.join(root, 'logs')), files);
});
