'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { createActivityLog } = require('../src/activity-log');
const { observeProcess, exitDescription, failureMessage } = require('../src/process-output');

function start(script, output, options = {}, stdio = ['ignore', 'pipe', 'pipe']) {
  const child = spawn(process.execPath, ['-e', script], { stdio });
  const result = observeProcess(child, { output, ...options });
  return { child, result };
}

test('captures both pipes, blank lines, CRLF, carriage-return progress and final unterminated diagnostics', async () => {
  const log = createActivityLog(), lines = [];
  const { result } = start(`
    process.stdin.on('end', () => {
      process.stdout.write('stdin closed\\n\\n[1/2] Building\\r\\n[2/2] Linking\\rstdout tail');
      process.stderr.write('\\x1b[31m[OSPanic] Café failed\\x1b[0m');
      process.exitCode = 7;
    });
    process.stdin.resume();
  `, log.write, { capture: true, line: (text, stream) => lines.push({ text, stream }) });
  const status = await result;
  assert.equal(status.code, 7);
  assert.equal(status.signal, null);
  assert.equal(status.error, null);
  assert.equal(status.stdout, 'stdin closed\n\n[1/2] Building\r\n[2/2] Linking\rstdout tail');
  assert.equal(status.stderr, '\x1b[31m[OSPanic] Café failed\x1b[0m');
  assert.deepEqual(log.snapshot().filter(entry => entry.stream === 'stdout').map(entry => entry.text),
    ['stdin closed', '', '[1/2] Building', '[2/2] Linking', 'stdout tail']);
  assert.deepEqual(log.snapshot().filter(entry => entry.stream === 'stderr').map(entry => entry.text), ['[OSPanic] Café failed']);
  assert.ok(lines.some(entry => entry.text === 'stdout tail'));
  assert.ok(lines.some(entry => entry.text === '[OSPanic] Café failed'));
});

test('partial output appears before process exit and split UTF-8 remains intact in log and capture', { timeout: 5000 }, async t => {
  const log = createActivityLog();
  let signalPartial;
  const partialReady = new Promise(resolve => { signalPartial = resolve; });
  const { child, result } = start(`
    const bytes = Buffer.from('Café 🌞');
    process.on('message', () => {
      process.stdout.write(bytes.subarray(4));
      process.stderr.write('last error');
      process.disconnect();
    });
    process.stdout.write(bytes.subarray(0, 4));
  `, (text, stream, previous) => {
    const entry = log.write(text, stream, previous);
    if (text === 'Caf') signalPartial();
    return entry;
  }, { capture: true }, ['ignore', 'pipe', 'pipe', 'ipc']);
  t.after(() => { if (child.exitCode === null) child.kill('SIGKILL'); });
  await partialReady;
  assert.equal(child.exitCode, null);
  assert.deepEqual(log.snapshot().map(entry => entry.text), ['Caf']);
  child.send('finish');
  const status = await result;
  assert.equal(status.stdout, 'Café 🌞');
  assert.equal(status.stderr, 'last error');
  assert.deepEqual(log.snapshot().filter(entry => entry.stream === 'stdout').map(entry => entry.text), ['Café 🌞']);
});

test('signal termination and spawn failure retain their actual cause', { timeout: 5000 }, async () => {
  const log = createActivityLog();
  const { child, result } = start('setInterval(() => {}, 1000)', log.write);
  child.kill('SIGTERM');
  const status = await result;
  assert.equal(status.code, null);
  assert.equal(status.signal, 'SIGTERM');
  assert.equal(exitDescription('Game', status.code, status.signal), 'Game terminated by SIGTERM');
  assert.equal(exitDescription('Build', 7, null), 'Build exited with code 7');

  const missing = spawn('/sms-launcher-nonexistent-command', [], { stdio: ['ignore', 'pipe', 'pipe'] });
  const failed = await observeProcess(missing, { output: log.write });
  assert.equal(failed.error.code, 'ENOENT');
});

test('Windows native statuses use hexadecimal and signed or unsigned codes mean the same exception', () => {
  for (const [code, name] of [[0xC0000374, 'heap corruption'], [0xC0000409, 'fast-fail'],
    [0xC00000FD, 'stack overflow'], [0xC0000005, 'access violation']]) {
    const hex = code.toString(16).toUpperCase();
    for (const value of [code, code | 0])
      assert.ok(exitDescription('Game', value, null, 'win32').includes(`(0x${hex}: ${name})`));
  }
});

test('game failure messages never blame normal resource warnings or stage context', () => {
  for (const lines of [['Could not find customStages.bin', 'Delay Context: 5'], ['Delay Context: 10']]) {
    assert.equal(failureMessage('Game', { code: 127, signal: null }, lines, { game: true, platform: 'win32' }),
      'Game exited with code 127. See the activity log.');
    assert.equal(failureMessage('Game', { code: 0xC0000374, signal: null }, lines, { game: true, platform: 'win32' }),
      'Game exited with code 3221226356 (0xC0000374: heap corruption). See the activity log.');
  }
  assert.match(failureMessage('Build', { code: 1, signal: null }, ['fatal error: missing header']), /missing header/);
});
