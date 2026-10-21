'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const mac = require('../src/mac-tools');
const tools = require('../src/build-tools');

function fixture({ prefix = '/opt/homebrew', missing = [], sdk = true, silicon = true,
  rosetta = true, cmakeVersion = '3.31.6', sdks = [], unlinkable = [] } = {}) {
  const files = new Set([
    ...['git', 'make', 'patch', 'curl', 'hdiutil', 'clang', 'clang++', 'python3'].map(name => `/usr/bin/${name}`),
    ...['brew', 'cmake', 'python3', '7zz'].map(name => `${prefix}/bin/${name}`),
    ...['llvm-objcopy', 'clang', 'clang++'].map(name => `${prefix}/opt/llvm/bin/${name}`)
  ].filter(file => !missing.includes(file.split('/').pop())));
  const calls = [];
  const exists = file => files.has(file);
  const list = directory => directory === '/SDK' ? sdks : [];
  const run = async (command, args, env) => {
    calls.push({ command, args, env });
    if (command === '/usr/bin/xcrun') return { ok: sdk, stdout: sdk ? '/SDK/MacOSX.sdk' : '' };
    if (command === '/usr/sbin/sysctl') return { ok: true, stdout: silicon ? '1' : '0' };
    if (command === '/usr/bin/arch') return { ok: rosetta, stdout: '' };
    if (args.includes('-isysroot')) return { ok: !unlinkable.includes(args[args.indexOf('-isysroot') + 1]), stdout: '' };
    if (!command || !exists(command)) return { ok: false, stdout: '' };
    return { ok: true, stdout: command.endsWith('/cmake') ? `cmake version ${cmakeVersion}` : 'version' };
  };
  return { exists, list, run, calls };
}

test('Finder PATH resolves Homebrew tools on both Mac architectures without changing the parent environment', async () => {
  for (const prefix of ['/opt/homebrew', '/usr/local']) {
    const base = { PATH: '/usr/bin:/bin:/usr/sbin:/sbin', HOME: '/Users/player' };
    const fake = fixture({ prefix, silicon: prefix === '/opt/homebrew' });
    const report = mac.report(await mac.inspect(base, fake), { archives: true });
    assert.equal(report.ready, true);
    assert.equal(report.mode, 'system');
    assert.ok(fake.calls.some(call => call.command === `${prefix}/bin/cmake`));
    assert.ok(fake.calls.some(call => call.command === `${prefix}/opt/llvm/bin/clang++`));
    assert.equal(base.PATH, '/usr/bin:/bin:/usr/sbin:/sbin');
  }
});

test('fresh Mac is not ready and gets instructions for every missing requirement', async () => {
  assert.equal(tools.status('/unused', 'darwin').ready, false);
  const fake = fixture({ sdk: false, rosetta: false,
    missing: ['cmake', 'python3', 'llvm-objcopy', '7zz', 'brew'] });
  const report = mac.report(await mac.inspect({ PATH: '/usr/bin:/bin' }, fake), { archives: true });
  assert.equal(report.ready, false);
  assert.equal(report.needsHomebrew, true);
  assert.equal(report.commands, 'xcode-select --install\nsoftwareupdate --install-rosetta\nbrew install cmake python3 llvm sevenzip');
  assert.match(report.message, /CMake/);
});

test('SDK and Rosetta are verified even when compiler executables exist', async () => {
  for (const options of [{ sdk: false }, { rosetta: false }]) {
    const report = mac.report(await mac.inspect({}, fixture(options)));
    assert.equal(report.ready, false);
    assert.deepEqual(report.requirements.filter(item => !item.ready && !item.optional).map(item => item.id),
      [options.sdk === false ? 'apple' : 'rosetta']);
  }
});

test('rejects old CMake and requires 7-Zip only for textures or Eclipse', async () => {
  const inspection = await mac.inspect({}, fixture({ missing: ['7zz'] }));
  assert.equal(mac.report(inspection).ready, true);
  assert.equal(mac.report(inspection, { archives: true }).ready, false);
  assert.equal(mac.report(inspection, { archives: true }).commands, 'brew install sevenzip');
  const old = mac.report(await mac.inspect({}, fixture({ cmakeVersion: '3.19.8' })));
  assert.equal(old.ready, false);
  assert.equal(old.commands, 'brew install cmake');
});

test('supports an explicit Homebrew prefix for tools installed outside the defaults', async () => {
  const fake = fixture({ prefix: '/custom/brew', silicon: false });
  assert.equal(mac.report(await mac.inspect({ HOMEBREW_PREFIX: '/custom/brew' }, fake)).ready, true);
});

test('private tools take priority and managed setup asks only for Apple installs', async () => {
  const fake = fixture({ sdk: false, rosetta: false, missing: ['cmake', 'python3', 'llvm-objcopy', '7zz', 'brew'] });
  const report = mac.report(await mac.inspect({ PATH: '/usr/bin:/bin' }, fake), { archives: true, managed: true });
  assert.equal(report.commands, 'xcode-select --install\nsoftwareupdate --install-rosetta');
  assert.equal(report.needsHomebrew, false);
  assert.ok(!fake.calls.some(call => ['/usr/bin/git', '/usr/bin/python3', '/usr/bin/clang', '/usr/bin/clang++'].includes(call.command)));
  const env = mac.environment({ PATH: '/usr/bin:/bin', SMS_BUILD_TOOLS_BIN: '/Users/player/Library/Application Support/SMS Launcher/build-tools/macos-arm64/env/bin' }, fixture().exists);
  assert.equal(env.PATH.split(':')[0], '/Users/player/Library/Application Support/SMS Launcher/build-tools/macos-arm64/env/bin');
});

test('keeps the default SDK when Apple\'s linker accepts it', async () => {
  const fake = fixture({ sdks: ['MacOSX.sdk', 'MacOSX26.5.sdk'] });
  const inspection = await mac.inspect({}, fake);
  assert.equal(inspection.sdkPath, '/SDK/MacOSX.sdk');
  const probes = fake.calls.filter(call => call.args.includes('-isysroot'));
  assert.deepEqual(probes.map(call => [call.command, call.env.SDKROOT]), [['/usr/bin/clang++', '/SDK/MacOSX.sdk']]);
  assert.ok(probes[0].args.includes('x86_64'));
});

test('uses the newest installed SDK that links when the default is newer than the linker', async () => {
  const fake = fixture({ sdks: ['MacOSX.sdk', 'MacOSX26.4.sdk', 'MacOSX26.5.sdk', 'MacOSX26.10.sdk', 'MacOSX27.0.sdk', 'notes.txt'],
    unlinkable: ['/SDK/MacOSX.sdk', '/SDK/MacOSX27.0.sdk', '/SDK/MacOSX26.10.sdk'] });
  const inspection = await mac.inspect({}, fake);
  assert.equal(inspection.sdkPath, '/SDK/MacOSX26.5.sdk');
  assert.deepEqual(fake.calls.filter(call => call.args.includes('-isysroot')).map(call => call.env.SDKROOT),
    ['/SDK/MacOSX.sdk', '/SDK/MacOSX27.0.sdk', '/SDK/MacOSX26.10.sdk', '/SDK/MacOSX26.5.sdk']);
  assert.equal(mac.report(inspection).ready, true);
});

test('falls back to the default SDK when no SDK links', async () => {
  const fake = fixture({ sdks: ['MacOSX27.0.sdk'], unlinkable: ['/SDK/MacOSX.sdk', '/SDK/MacOSX27.0.sdk'] });
  assert.equal((await mac.inspect({}, fake)).sdkPath, '/SDK/MacOSX.sdk');
});
