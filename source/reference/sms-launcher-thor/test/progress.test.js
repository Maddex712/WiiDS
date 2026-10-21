'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { activityFromLine, cleanOutputLine, crashReason, createActivityReader, createLineReader, failureReason } = require('../src/progress');

test('shows real percentages for build and source downloads', () => {
  assert.deepEqual(activityFromLine('[ 42%] Building C object'), { detail: 'Building your game', percent: 42 });
  assert.deepEqual(activityFromLine('Receiving objects:  76% (123/160)'), { detail: 'Downloading files', percent: 76 });
  assert.deepEqual(activityFromLine('-- Configuring done'), { detail: 'Getting your game ready', percent: null });
});

test('uses Ninja completed steps on Windows and private Mac tools, including small incremental builds', () => {
  assert.deepEqual(activityFromLine('[735/2475] Building CXX object decomp/src/Camera/Camera.cpp.obj'),
    { detail: 'Building your game · 735 of 2,475 steps', percent: 29 });
  assert.deepEqual(activityFromLine('\x1b[2K\x1b[32m[2/3] Linking CXX executable sms.exe\x1b[0m'),
    { detail: 'Finishing the game build · 2 of 3 steps', percent: 66 });
  assert.deepEqual(activityFromLine('[3/3] Linking CXX executable sms'),
    { detail: 'Finishing the game build · 3 of 3 steps', percent: 100 });
  for (const invalid of ['[1/0] Building', '[4/3] Building', '[9007199254740992/9007199254740992] Building'])
    assert.equal(activityFromLine(invalid), null);
});

test('disc preparation remains a separate phase after compilation reaches 100 percent', () => {
  assert.deepEqual(activityFromLine('[100%] Bundling /Users/player/Game.iso into sms-standalone'),
    { detail: 'Preparing your game files', percent: null });
  assert.deepEqual(activityFromLine('[1/1] Bundling C:/Games/Game.iso into sms-standalone'),
    { detail: 'Preparing your game files', percent: null });
  assert.deepEqual(activityFromLine('Built build/windows-64/sms.exe (windows, 64-bit)'),
    { detail: 'Finishing setup', percent: null });
  assert.deepEqual(activityFromLine('bundle_disc: wrote build/linux-64/sms-standalone (512 MiB)'),
    { detail: 'Preparing your game files', percent: null });
});

test('reads pipe chunks, carriage-return progress, split Unicode, and the final error line without loss', () => {
  const output = Buffer.from('\x1b[2K[1/3] Building CXX object Mario.cpp.obj\r\n[2/3] Building CXX object Café.cpp.obj\r[3/3] Linking sms.exe\nerror: setup failed');
  const lines = [];
  const reader = createLineReader(line => lines.push(line));
  for (const byte of output) reader.write(Buffer.from([byte]));
  reader.end();
  assert.deepEqual(lines, ['[1/3] Building CXX object Mario.cpp.obj', '[2/3] Building CXX object Café.cpp.obj',
    '[3/3] Linking sms.exe', 'error: setup failed']);
  assert.equal(activityFromLine(lines[1]).percent, 66);
  assert.equal(cleanOutputLine('\x1b]8;;https://example.com\x1b\\Camera.cpp\x1b]8;;\x1b\\'), 'Camera.cpp');
});

test('reports texture install phases without inventing a download percentage', () => {
  assert.deepEqual(activityFromLine('Downloading Super Mario Sunshine UHD Texture Pack v2.1.1'),
    { detail: 'Downloading HD textures (about 1 GB)', percent: null });
  assert.deepEqual(activityFromLine('Downloading Super Mario Sunshine HD texture extras v1.1.0'),
    { detail: 'Downloading HD texture extras', percent: null });
  assert.deepEqual(activityFromLine('Unpacking GMS.7z'), { detail: 'Installing HD textures', percent: null });
  assert.deepEqual(activityFromLine('Installed 2180 textures in mods/textures/GMS.'),
    { detail: 'HD textures installed', percent: 100 });
});

test('reports texture download bytes, handles unknown file sizes, and separates checksum checking', () => {
  assert.deepEqual(activityFromLine('Texture download: 500000000/1000000000 bytes'),
    { detail: 'Downloading HD textures · 500 MB of 1,000 MB', percent: 50 });
  assert.deepEqual(activityFromLine('Texture download: 1000000000/1000000000 bytes'),
    { detail: 'Downloading HD textures · 1,000 MB of 1,000 MB', percent: 100 });
  assert.deepEqual(activityFromLine('Texture download: 500000000/0 bytes'),
    { detail: 'Downloading HD textures · 500 MB downloaded', percent: null });
  assert.deepEqual(activityFromLine('Checking HD texture download'),
    { detail: 'Checking HD textures', percent: null });
  for (const invalid of ['Texture download: 4/3 bytes', 'Texture download: 9007199254740992/0 bytes'])
    assert.equal(activityFromLine(invalid), null);
});


test('HD movie setup reports its own download and complete-movie progress', () => {
  assert.deepEqual(activityFromLine('Preparing HD cutscenes from your disc'),
    { detail: 'Preparing HD cutscenes from your disc', percent: null });
  assert.deepEqual(activityFromLine('HD movie download: 1500000/3000000 bytes'),
    { detail: 'Downloading HD cutscenes · 1.5 of 3.0 MB', percent: 50 });
  assert.deepEqual(activityFromLine('Installing HD cutscenes: 7/21 movies'),
    { detail: 'Installing HD cutscenes · 7 of 21 movies', percent: 33 });
  assert.deepEqual(activityFromLine('HD cutscenes installed: 21/21 movies'),
    { detail: 'HD cutscenes installed', percent: 100 });
  assert.equal(activityFromLine('HD movie download: 4000000/3000000 bytes'), null);
  assert.deepEqual(activityFromLine('HD movie download: 1500000/3000000 bytes (movie 8/21)'),
    { detail: 'Downloading HD cutscenes · movie 8 of 21 · 1.5 of 3.0 MB', percent: 35 });
  assert.equal(activityFromLine('HD movie download: 1500000/3000000 bytes (movie 22/21)'), null);
});

test('failed tasks report the line that explains them', () => {
  assert.equal(failureReason([
    '-- SMS port: building -m64',
    'CMake Error at cmake/eclipse.cmake:17 (message):',
    "  SMS_ECLIPSE needs clang/clang++ (Eclipse's sources are written for clang)",
    'Call Stack (most recent call first):',
    '  CMakeLists.txt:410 (include)',
    '-- Configuring incomplete, errors occurred!'
  ]), "SMS_ECLIPSE needs clang/clang++ (Eclipse's sources are written for clang)");
  assert.equal(failureReason([
    '[12/900] Building CXX object a.obj',
    'FAILED: b.obj',
    "b.cpp:3:1: error: unknown type name 'foo'",
    '1 error generated.',
    'ninja: build stopped: subcommand failed.'
  ]), "b.cpp:3:1: error: unknown type name 'foo'");
  assert.equal(failureReason(['Eclipse needs clang and clang++. Update the launcher, then try again.']),
    'Eclipse needs clang and clang++. Update the launcher, then try again');
  assert.equal(failureReason([]), '');
});

test('names each Eclipse source while git downloads it', () => {
  const read = createActivityReader();
  assert.deepEqual(read('-- SMS_ECLIPSE: fetching bse fd6273014545ac0174fa54fada02edd9212f63d8'),
    { detail: 'Downloading Eclipse sources · Better Sunshine Engine (2 of 4)', percent: null });
  assert.deepEqual(read('remote: Counting objects: 100% (211/211), done.'),
    { detail: 'Downloading Eclipse sources · Better Sunshine Engine (2 of 4) · waiting for GitHub', percent: null });
  assert.equal(read('From https://github.com/JoshuaMKW/BetterSunshineEngine'), null);
  assert.deepEqual(read('Receiving objects:  94% (199/211), 45.39 MiB | 4.24 MiB/s'),
    { detail: 'Downloading Eclipse sources · Better Sunshine Engine (2 of 4) · 45.39 MiB at 4.24 MiB/s', percent: 94 });
  assert.deepEqual(read('Receiving objects:   3% (7/211)'),
    { detail: 'Downloading Eclipse sources · Better Sunshine Engine (2 of 4)', percent: 3 });
  assert.deepEqual(read('Updating files:  50% (94/188)'),
    { detail: 'Writing Eclipse sources · Better Sunshine Engine (2 of 4)', percent: 50 });
  assert.deepEqual(read('-- Configuring done'), { detail: 'Getting your game ready', percent: null });
  assert.deepEqual(read('Receiving objects:  76% (123/160)'), { detail: 'Downloading files', percent: 76 });
});

test('a crash is reported as a crash, not as the last warning the game printed', () => {
  // Windows: MSYS bash's report of a native crash, or the exception status itself.
  assert.equal(crashReason(2816, null, 'win32'), 'crashed with a memory access error');
  assert.equal(crashReason(2560, null, 'win32'), 'crashed with a memory access error');
  assert.equal(crashReason(0xC0000005, null, 'win32'), 'crashed with a memory access error');
  assert.equal(crashReason(-1073741819, null, 'win32'), 'crashed with a memory access error');
  // Linux: the re-raised signal. macOS: the game's 128 + signal exit.
  assert.equal(crashReason(null, 'SIGSEGV', 'linux'), 'crashed with a memory access error');
  assert.equal(crashReason(null, 'SIGABRT', 'linux'), 'crashed with an internal error');
  assert.equal(crashReason(139, null, 'darwin'), 'crashed with a memory access error');
  assert.equal(crashReason(138, null, 'darwin'), 'crashed with a memory access error');
  assert.equal(crashReason(135, null, 'linux'), 'crashed with a memory access error');
  for (const [code, signal, platform] of [[1, null, 'win32'], [2, null, 'linux'], [256, null, 'win32'], [139, null, 'win32'],
    [null, 'SIGTERM', 'linux'], [0, null, 'darwin'], [130, null, 'darwin']])
    assert.equal(crashReason(code, signal, platform), null);
});

test('shows decomp patching as progress, which can be slow on some computers', () => {
  assert.deepEqual(activityFromLine('-- SMS port: applying decomp patches (1/213)'),
    { detail: "Preparing the game's source · patch 1 of 213", percent: 0 });
  assert.deepEqual(activityFromLine('-- SMS port: applying decomp patches (213/213)'),
    { detail: "Preparing the game's source · patch 213 of 213", percent: 100 });
  for (const invalid of ['-- SMS port: applying decomp patches (5/0)', '-- SMS port: applying decomp patches (9/3)'])
    assert.equal(activityFromLine(invalid), null);
  assert.deepEqual(activityFromLine('-- SMS port: 213 decomp patch(es) applied'), null);
});
