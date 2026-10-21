'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const { createRequire } = require('node:module');

function main(t, { platform = process.platform, childProcess } = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-main-log-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const filename = path.resolve(__dirname, '../src/main.js');
  const localRequire = createRequire(filename);
  const appEvents = {}, processEvents = {}, consoleEvents = {}, handlers = {}, copied = [];
  const electron = { ipcMain: { handle: (name, handler) => { handlers[name] = handler; } },
    clipboard: { writeText: text => copied.push(text) },
    app: { requestSingleInstanceLock: () => true, on: (name, handler) => { appEvents[name] = handler; },
    whenReady: () => ({ then() {} }), getPath: () => root },
    screen: { getPrimaryDisplay: () => ({ scaleFactor: 1, bounds: { x: 0, y: 0, width: 1920, height: 1080 } }), getDisplayMatching: () => ({ scaleFactor: 1, bounds: { x: 0, y: 0, width: 1920, height: 1080 } }), on() {} } };
  electron.BrowserWindow = class {
    webContents = { send() {}, setWindowOpenHandler() {}, on: (name, handler) => { consoleEvents[name] = handler; } };
    isDestroyed() { return false; }
    getBounds() { return { x: 0, y: 0, width: 1080, height: 760 }; }
    hookWindowMessage() {}
    on() {}
    once() {}
    setMenuBarVisibility() {}
    loadFile() {}
  };
  const context = { require: id => id === 'electron' ? electron
      : id === 'node:child_process' && childProcess ? childProcess : localRequire(id),
    __dirname: path.dirname(filename),
    process: new Proxy(process, { get(target, key) {
      if (key === 'platform') return platform;
      return key === 'on' ? (name, handler) => { processEvents[name] = handler; } : target[key];
    } }),
    setTimeout, setInterval, module: { exports: {} } };
  vm.runInNewContext(fs.readFileSync(filename, 'utf8') +
    '\nmodule.exports = { launch, capture, exclusive, createWindow, registerHandlers, log, logOutput, activityLog, saveLog: file => sessionLog.save(file, [...partialOutput.values()]) };', context, { filename });
  return { ...context.module.exports, root, appEvents, processEvents, consoleEvents, handlers, copied };
}

test('Copy log IPC copies the full session with live partial output, beyond the view limit', t => {
  const runtime = main(t);
  runtime.registerHandlers();
  runtime.log('early diagnostic');
  for (let i = 0; i < 1000; ++i) runtime.log(`entry ${i}`);
  runtime.logOutput('live stderr diagnostic', 'stderr');
  runtime.logOutput('live stdout prompt', 'stdout');
  assert.equal(runtime.activityLog.snapshot().length, 700);
  runtime.handlers['copy-activity-log']();
  assert.match(runtime.copied[0], /^\[launcher\] early diagnostic\n/);
  assert.match(runtime.copied[0], /entry 999\n\[stderr\] live stderr diagnostic\n\[stdout\] live stdout prompt\n$/);
  const destination = path.join(runtime.root, 'saved.log');
  runtime.saveLog(destination);
  assert.equal(runtime.copied[0], fs.readFileSync(destination, 'utf8'));
  runtime.handlers['copy-activity-log']();
  assert.equal(runtime.copied[1], runtime.copied[0]);
});

test('all Play actions reset stored history and partial output before reporting setup errors', async t => {
  const runtime = main(t);
  runtime.registerHandlers();
  for (const channel of ['play', 'launch-game', 'play-previous']) {
    runtime.log('previous run');
    runtime.logOutput('previous partial', 'stderr');
    // No installation is configured: even this setup failure starts a fresh log.
    await assert.rejects(runtime.handlers[channel]());
    runtime.handlers['copy-activity-log']();
    const copied = runtime.copied.at(-1);
    assert.doesNotMatch(copied, /previous run|previous partial/);
    assert.match(copied, /\[launcher\] ✕/);
    assert.ok(runtime.activityLog.snapshot().every(entry => !['previous run', 'previous partial'].includes(entry.text)));
  }
});

test('a rejected Play while busy preserves the active session log', async t => {
  const runtime = main(t);
  runtime.registerHandlers();
  let finish;
  const running = runtime.exclusive('Running task', () => new Promise(resolve => { finish = resolve; }));
  runtime.log('active run diagnostic');
  await assert.rejects(runtime.handlers.play(), /Wait for the current task/);
  runtime.handlers['copy-activity-log']();
  assert.match(runtime.copied[0], /active run diagnostic/);
  finish();
  await running;
});

test('real launcher game failures preserve status without quoting warnings and can launch again', async t => {
  const runtime = main(t);
  await assert.rejects(runtime.launch(process.execPath, ['-e',
    "process.stderr.write('Could not find customStages.bin\\nDelay Context: 5');process.exitCode=7"], {}, 'Game', true),
    /^Error: Game exited with code 7\. See the activity log\.$/);
  assert.equal((await runtime.launch(process.execPath, ['-e', "process.stdout.write('ok')"], {}, 'Next')).ok, true);
  const entries = runtime.activityLog.snapshot();
  assert.ok(entries.some(entry => entry.stream === 'stderr' && entry.text === 'Delay Context: 5'));
  assert.ok(entries.some(entry => entry.stream === 'launcher' && entry.text === '✕ Game exited with code 7. See the activity log.'));
  const destination = path.join(runtime.root, 'saved.log');
  runtime.saveLog(destination);
  const saved = fs.readFileSync(destination, 'utf8');
  assert.match(saved, /\[stderr\] Could not find customStages.bin\n\[stderr\] Delay Context: 5\n/);
  assert.match(saved, /\[stdout\] ok\n\[launcher\] ✓ Next finished/);
});

test('the launcher logs captured commands and errors while preserving their returned stdout', async t => {
  const runtime = main(t);
  assert.equal(await runtime.capture(process.execPath, ['-e',
    "process.stdout.write('Café 🌞\\n');process.stderr.write('warning\\n')"]), 'Café 🌞');
  await assert.rejects(runtime.capture(process.execPath, ['-e',
    "process.stderr.write('fatal: real failure');process.exitCode=9"]), /code 9: fatal: real failure/);
  await assert.rejects(runtime.launch('/sms-launcher-missing-command', [], {}, 'Missing'), /ENOENT/);
  assert.equal((await runtime.launch(process.execPath, ['-e', ''], {}, 'Recovered')).ok, true);
  const entries = runtime.activityLog.snapshot();
  assert.ok(entries.some(entry => entry.stream === 'stdout' && entry.text === 'Café 🌞'));
  assert.ok(entries.some(entry => entry.stream === 'stderr' && entry.text === 'fatal: real failure'));
  assert.ok(entries.some(entry => entry.stream === 'launcher' && entry.text.includes('ENOENT')));
});

test('launcher exceptions, renderer errors and Electron process crashes are kept in the session log', t => {
  const runtime = main(t);
  runtime.createWindow();
  runtime.processEvents.uncaughtExceptionMonitor(new Error('launcher failed'));
  runtime.processEvents.warning(new Error('launcher warning'));
  runtime.consoleEvents['console-message']({ level: 'error', message: 'renderer failed', sourceId: 'renderer.js', lineNumber: 42 });
  runtime.appEvents['render-process-gone']({}, {}, { reason: 'crashed', exitCode: 9 });
  runtime.appEvents['child-process-gone']({}, { type: 'GPU', reason: 'oom', exitCode: 1 });
  const destination = path.join(runtime.root, 'saved.log');
  runtime.saveLog(destination);
  const saved = fs.readFileSync(destination, 'utf8');
  for (const expected of ['launcher failed', 'launcher warning', 'renderer failed (renderer.js:42)',
    'renderer crashed: Renderer exited with code 9', 'GPU process oom: GPU exited with code 1'])
    assert.ok(saved.includes(expected), expected);
});

test('Windows starts the game detached, not hidden, so its window shows', async t => {
  const { EventEmitter } = require('node:events');
  const { PassThrough } = require('node:stream');
  const spawned = [];
  const childProcess = { spawn(command, args, options) {
    spawned.push({ command, options });
    const child = Object.assign(new EventEmitter(), { stdout: new PassThrough(), stderr: new PassThrough(), pid: 1 });
    setImmediate(() => { child.stdout.end(); child.stderr.end(); child.emit('close', 0, null); });
    return child;
  } };
  const runtime = main(t, { platform: 'win32', childProcess });
  await runtime.launch('sms.exe', [], {}, 'Play Super Mario Sunshine', true);
  await runtime.launch('bash.exe', [], {}, 'Build game');
  assert.equal(spawned[0].options.windowsHide, false);
  assert.equal(spawned[0].options.detached, true);
  assert.equal(spawned[1].options.windowsHide, true);
  assert.equal(spawned[1].options.detached, false);
});
