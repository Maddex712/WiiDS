'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const tar = require('tar');
const tools = require('../src/build-tools');
const x64Tools = { skip: process.arch !== 'x64' && 'Linux/Windows tool archives require an x64 host.' };

test('Linux private Make builds a nested CMake project when the tools path contains spaces', {
  skip: process.platform !== 'linux'
}, t => {
  const cmake = spawnSync('sh', ['-c', 'command -v cmake'], { encoding: 'utf8' }).stdout.trim();
  const make = spawnSync('sh', ['-c', 'command -v make'], { encoding: 'utf8' }).stdout.trim();
  if (!cmake || !make) return t.skip('This integration check requires CMake and GNU Make.');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms private tools with spaces-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const bin = path.join(root, 'env', 'bin');
  const source = path.join(root, 'project');
  const child = path.join(source, 'child');
  fs.mkdirSync(bin, { recursive: true });
  fs.mkdirSync(child, { recursive: true });
  fs.symlinkSync(make, path.join(bin, 'make'));
  fs.writeFileSync(path.join(source, 'CMakeLists.txt'), `
    cmake_minimum_required(VERSION 3.20)
    project(path_probe NONE)
    include(ExternalProject)
    ExternalProject_Add(child SOURCE_DIR "\${CMAKE_CURRENT_SOURCE_DIR}/child"
      BINARY_DIR "\${CMAKE_CURRENT_BINARY_DIR}/child" INSTALL_COMMAND "")
  `);
  fs.writeFileSync(path.join(child, 'CMakeLists.txt'), `
    cmake_minimum_required(VERSION 3.20)
    project(child NONE)
    add_custom_target(path_probe ALL COMMAND "\${CMAKE_COMMAND}" -E touch "\${CMAKE_BINARY_DIR}/passed")
  `);
  const build = path.join(root, 'build');
  const env = tools.environmentAtRoot(root, { ...process.env, PATH: `${bin}:${process.env.PATH}` }, 'linux');
  for (const args of [
    ['-S', source, '-B', build, '-G', 'Unix Makefiles', `-DCMAKE_MAKE_PROGRAM:FILEPATH=${path.join(bin, 'make')}`],
    ['--build', build]
  ]) {
    const result = spawnSync(cmake, args, { env, encoding: 'utf8', timeout: 30000 });
    assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
  }
  assert.ok(fs.existsSync(path.join(build, 'child', 'passed')));
});

test('a corrupt tool archive preserves the existing tools', x64Tools, async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-tools-test-'));
  try {
    const root = tools.rootFor(dir, 'linux');
    fs.mkdirSync(root, { recursive: true });
    fs.writeFileSync(path.join(root, 'keep.txt'), 'existing tools');
    const archive = path.join(dir, 'tools.tar.gz');
    fs.writeFileSync(archive, 'corrupt archive');
    await assert.rejects(tools.prepare(dir, { platform: 'linux', forcePrivate: true,
      archiveFile: archive, source: { name: 'tools.tar.gz', sha256: '0'.repeat(64) } }), /SHA-256/);
    assert.equal(fs.readFileSync(path.join(root, 'keep.txt'), 'utf8'), 'existing tools');
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test('failed archive relocation restores the previous tools', x64Tools, async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-tools-test-'));
  try {
    const root = tools.rootFor(dir, 'linux');
    fs.mkdirSync(root, { recursive: true });
    fs.writeFileSync(path.join(root, 'keep.txt'), 'existing tools');
    const staging = path.join(dir, 'fixture');
    fs.mkdirSync(path.join(staging, 'env'), { recursive: true });
    fs.writeFileSync(path.join(staging, 'env', 'payload'), 'new tools');
    const archive = path.join(dir, 'tools.tar.gz');
    await tar.c({ gzip: true, file: archive, cwd: staging }, ['env']);
    await assert.rejects(tools.prepare(dir, { platform: 'linux', forcePrivate: true,
      archiveFile: archive, source: { name: 'tools.tar.gz', sha256: await tools.hashFile(archive) },
      run: async () => { throw new Error('relocation failed'); } }), /relocation failed/);
    assert.equal(fs.readFileSync(path.join(root, 'keep.txt'), 'utf8'), 'existing tools');
    assert.equal(fs.existsSync(path.join(root, 'env')), false);
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test('matching legacy tools are reused, while changed archives get a separate root and leave old tools intact', t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-tool-versions-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const legacy = tools.legacyRootFor(dir, 'linux');
  fs.mkdirSync(legacy, { recursive: true });
  fs.writeFileSync(path.join(legacy, 'ready.json'), JSON.stringify({ toolset: tools.toolsetFor('linux'), archiveSha256: tools.assetFor('linux').sha256 }));
  fs.writeFileSync(path.join(legacy, 'keep.txt'), 'libraries for my older game');
  assert.equal(tools.rootFor(dir, 'linux'), legacy);
  fs.writeFileSync(path.join(legacy, 'ready.json'), JSON.stringify({ toolset: 'older', archiveSha256: '0'.repeat(64) }));
  assert.notEqual(tools.rootFor(dir, 'linux'), legacy);
  assert.equal(fs.readFileSync(path.join(legacy, 'keep.txt'), 'utf8'), 'libraries for my older game');
});

test('fallback play can use a recorded older Windows toolset without downloading the current tools', t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-old-tools-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  fs.mkdirSync(path.join(dir, 'msys64'));
  const env = tools.environmentAtRoot(dir, { PATH: 'existing path', SMS_ARCH: '32' }, 'win32');
  assert.equal(env.MSYS2_ROOT, path.join(dir, 'msys64'));
  assert.equal(env.MSYSTEM, 'MINGW32');
  assert.ok(env.PATH.includes(path.join(dir, 'msys64', 'usr', 'bin')));
  const current = tools.environmentAtRoot(dir, { PATH: 'existing path', SMS_ARCH: '64' }, 'win32');
  assert.equal(current.MSYSTEM, 'MINGW64');
  assert.ok(current.PATH.startsWith(path.join(dir, 'msys64', 'mingw64', 'bin')));
  assert.equal(current.PATH.includes(path.join(dir, 'msys64', 'mingw32', 'bin')), false);
});

test('Windows build keeps the system Path that a copied environment spells Path', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-path-tools-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'msys64', 'usr', 'bin'), { recursive: true });
  fs.writeFileSync(path.join(root, 'msys64', 'usr', 'bin', 'bash.exe'), '');
  // A copy of process.env on Windows, as port.buildEnvironment makes it.
  const env = tools.environmentAtRoot(root, { Path: 'C:\\WINDOWS\\system32', SMS_ARCH: '64' }, 'win32');
  assert.deepEqual(Object.keys(env).filter(key => key.toUpperCase() === 'PATH'), ['PATH']);
  assert.ok(env.PATH.startsWith(path.join(root, 'msys64', 'mingw64', 'bin')));
  assert.ok(env.PATH.endsWith(';C:\\WINDOWS\\system32'));
  const port = require('../src/port');
  for (const command of [port.commandFor(root, 'build', [], 'win32', { ...env, Path: 'stale' }),
    port.commandFor(root, 'build', [], 'win32', { Path: 'C:\\WINDOWS\\system32', MSYS2_ROOT: path.join(root, 'msys64') }),
    port.commandFor(root, 'run', ['disc.iso'], 'win32', { Path: 'C:\\WINDOWS\\system32', MSYS2_ROOT: path.join(root, 'msys64') })]) {
    assert.deepEqual(Object.keys(command.env).filter(key => key.toUpperCase() === 'PATH'), ['PATH']);
    assert.ok(command.env.PATH.endsWith(';C:\\WINDOWS\\system32'));
  }
});

test('32-bit Windows target uses x64 helpers and a cross compiler, with target DLLs first', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-cross-tools-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const cross = path.join(root, 'msys64', 'opt', 'bin', 'i686-w64-mingw32-g++.exe');
  fs.mkdirSync(path.dirname(cross), { recursive: true });
  fs.writeFileSync(cross, 'fixture');
  const env = tools.environmentAtRoot(root, { PATH: 'system', SMS_ARCH: '32' }, 'win32');
  assert.equal(env.MSYSTEM, 'MINGW64');
  assert.equal(env.SMS_WINDOWS_32_CROSS, '1');
  assert.equal(env.CXX, cross);
  assert.equal(env.CMAKE_PREFIX_PATH, path.join(root, 'msys64', 'mingw32'));
  assert.ok(env.PATH.startsWith(path.join(root, 'msys64', 'opt', 'i686-w64-mingw32', 'bin')));
  fs.mkdirSync(path.join(root, 'msys64', 'usr', 'bin'), { recursive: true });
  fs.writeFileSync(path.join(root, 'msys64', 'usr', 'bin', 'bash.exe'), '');
  const command = require('../src/port').commandFor(root, 'build', [], 'win32', env);
  assert.equal(command.env.MSYSTEM, 'MINGW64');
  assert.equal(command.env.PATH, env.PATH);
});

test('32-bit Linux target selects the private SDK without changing the host helper library path', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-cross-tools-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const sdk = path.join(root, 'env', 'targets', 'linux32');
  fs.mkdirSync(path.join(sdk, 'bin'), { recursive: true });
  fs.writeFileSync(path.join(sdk, 'bin', 'i686-linux-g++'), 'fixture');
  const env = tools.environmentAtRoot(root, { PATH: 'system', SMS_ARCH: '32' }, 'linux');
  assert.equal(env.CXX, path.join(sdk, 'bin', 'i686-linux-g++'));
  assert.equal(env.SMS_LINUX32_ROOT, sdk);
  assert.equal(env.LD_LIBRARY_PATH, undefined);
  assert.ok(env.PATH.startsWith(path.join(root, 'env', 'bin')));
  assert.ok(env.SMS_LINUX32_LIBRARY_PATH.includes('i386-linux-gnu'));
  const root64 = tools.environmentAtRoot(root, { SMS_ARCH: '64' }, 'linux');
  assert.equal(root64.CXX, path.join(root, 'env', 'bin', 'g++'));
  assert.equal(root64.SMS_LINUX32_ROOT, undefined);
});
