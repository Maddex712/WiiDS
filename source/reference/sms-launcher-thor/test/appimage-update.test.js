'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { replaceAppImage, keepAppImagePath } = require('../src/appimage-update');

// A player's AppImage (named with a version, as releases were) and a
// downloaded update in electron-updater's cache.
function setup(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'appimage-update-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const games = path.join(dir, 'Games');
  const pending = path.join(dir, 'cache', 'pending');
  fs.mkdirSync(games, { recursive: true });
  fs.mkdirSync(pending, { recursive: true });
  const appImage = path.join(games, 'SMS-Launcher-0.1.45.AppImage');
  const downloaded = path.join(pending, 'SMS-Launcher.AppImage');
  fs.writeFileSync(appImage, 'old');
  fs.writeFileSync(downloaded, 'new');
  return { games, appImage, downloaded };
}

// fs, with renames from the download failing as they do across disks
function otherDisk(downloaded, overrides = {}) {
  return { ...fs, ...overrides, renameSync(from, to) {
    if (from === downloaded) throw Object.assign(new Error('cross-device link'), { code: 'EXDEV' });
    return fs.renameSync(from, to);
  } };
}

test('an update goes over the AppImage the player started, under its own name', t => {
  const { games, appImage, downloaded } = setup(t);
  const runs = [];
  const updater = { installerPath: downloaded };
  keepAppImagePath(updater, { env: { APPIMAGE: appImage, HOME: '/home/deck' },
    run: (file, args, options) => { runs.push({ file, args, options }); return { unref() {} }; } });
  assert.equal(updater.doInstall({ isSilent: true, isForceRunAfter: true }), true);
  assert.equal(fs.readFileSync(appImage, 'utf8'), 'new');
  assert.deepEqual(fs.readdirSync(games), ['SMS-Launcher-0.1.45.AppImage']);
  assert.equal(fs.existsSync(downloaded), false);
  assert.equal(runs.length, 1);
  assert.equal(runs[0].file, appImage);
  assert.equal(runs[0].options.detached, true);
  assert.equal(runs[0].options.env.APPIMAGE_SILENT_INSTALL, 'true');
  assert.equal(runs[0].options.env.HOME, '/home/deck');
});

test('installing when the launcher quits lets the new AppImage finish and exit', t => {
  const { appImage, downloaded } = setup(t);
  const runs = [];
  const updater = { installerPath: downloaded };
  keepAppImagePath(updater, { env: { APPIMAGE: appImage }, runSync: (file, args, options) => runs.push({ file, options }) });
  assert.equal(updater.doInstall({ isSilent: true, isForceRunAfter: false }), true);
  assert.equal(runs[0].file, appImage);
  assert.equal(runs[0].options.env.APPIMAGE_EXIT_AFTER_INSTALL, 'true');
});

test('from another disk it copies beside the old AppImage, then swaps it in', t => {
  const { games, appImage, downloaded } = setup(t);
  replaceAppImage(downloaded, appImage, otherDisk(downloaded));
  assert.equal(fs.readFileSync(appImage, 'utf8'), 'new');
  assert.deepEqual(fs.readdirSync(games), ['SMS-Launcher-0.1.45.AppImage']);
  assert.equal(fs.existsSync(downloaded), false);
});

test('a failed copy leaves the player\'s AppImage as it was', t => {
  const { games, appImage, downloaded } = setup(t);
  const files = otherDisk(downloaded, { copyFileSync(from, to) {
    fs.writeFileSync(to, 'ne');
    throw Object.assign(new Error('no space left on device'), { code: 'ENOSPC' });
  } });
  assert.throws(() => replaceAppImage(downloaded, appImage, files), /no space/);
  assert.equal(fs.readFileSync(appImage, 'utf8'), 'old');
  assert.deepEqual(fs.readdirSync(games), ['SMS-Launcher-0.1.45.AppImage']);
});

test('without the AppImage path nothing is replaced', t => {
  const { appImage, downloaded } = setup(t);
  for (const env of [{}, { APPIMAGE: 'relative/SMS-Launcher.AppImage' }]) {
    const updater = { installerPath: downloaded };
    keepAppImagePath(updater, { env, run: () => assert.fail('started'), runSync: () => assert.fail('started') });
    assert.throws(() => updater.doInstall({ isForceRunAfter: true }), /no AppImage/);
  }
  assert.equal(fs.readFileSync(appImage, 'utf8'), 'old');
  assert.equal(fs.readFileSync(downloaded, 'utf8'), 'new');
});

// keepAppImagePath replaces doInstall, so an electron-updater that installs
// AppImages some other way would quietly bring back the renaming.
test('electron-updater still installs AppImages through doInstall', () => {
  const out = path.join(__dirname, '..', 'node_modules', 'electron-updater', 'out');
  assert.match(fs.readFileSync(path.join(out, 'AppImageUpdater.js'), 'utf8'), /\bdoInstall\(options\)\s*\{/);
  assert.match(fs.readFileSync(path.join(out, 'BaseUpdater.js'), 'utf8'), /return this\.doInstall\(\{/);
});
