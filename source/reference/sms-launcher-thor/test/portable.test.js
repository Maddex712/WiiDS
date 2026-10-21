'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const portable = require('../src/portable');
const saves = require('../src/saves');

const exeDir = path.resolve('/usb/games');
const env = { PORTABLE_EXECUTABLE_DIR: exeDir, APPDATA: path.resolve('/appdata') };
const root = path.join(exeDir, 'SMS-Launcher-Portable');

test('only the Windows portable build gets a portable folder beside the exe', () => {
  assert.equal(portable.root(env, 'win32'), root);
  assert.equal(portable.root({}, 'win32'), null);
  assert.equal(portable.root(env, 'linux'), null);
});

test('portable saves and backups stay in the portable folder', () => {
  const repo = path.join(root, 'sms-pc-port');
  assert.equal(saves.saveDirectory(repo, env, 'win32'), path.join(root, 'saves', 'card-a'));
  assert.equal(saves.saveDirectory(repo, { ...env, SMS_SAVE_DIR: 'custom' }, 'win32'), path.join(repo, 'custom'));
  assert.equal(saves.backupRoot(env, 'win32'), path.join(root, 'Save Backups'));
  assert.notEqual(saves.backupRoot({}, 'win32'), path.join(root, 'Save Backups'));
});

test('paths inside the portable folder are saved relative and follow it when moved', () => {
  const outside = path.resolve('/discs/sms.iso');
  const config = { repo: path.join(root, 'sms-pc-port'), rom: outside, installRoot: null, saveDirectory: root,
    settings: { arch: '64' }, previousInstall: { repo: path.join(root, 'old'), rom: path.join(root, 'sms.iso') } };
  const stored = portable.storedPaths(config, root);
  assert.equal(stored.repo, 'sms-pc-port');
  assert.equal(stored.rom, outside);
  assert.equal(stored.installRoot, null);
  assert.equal(stored.saveDirectory, '.');
  assert.deepEqual(stored.settings, { arch: '64' });
  assert.deepEqual(stored.previousInstall, { repo: 'old', rom: 'sms.iso' });
  assert.equal(config.repo, path.join(root, 'sms-pc-port'));

  const moved = path.resolve('/other/SMS-Launcher-Portable');
  const loaded = portable.resolvedPaths(JSON.parse(JSON.stringify(stored)), moved);
  assert.equal(loaded.repo, path.join(moved, 'sms-pc-port'));
  assert.equal(loaded.rom, outside);
  assert.equal(loaded.saveDirectory, moved);
  assert.equal(loaded.previousInstall.rom, path.join(moved, 'sms.iso'));
});

test('portable games keep their shader cache in the portable folder', () => {
  assert.equal(portable.shaderCache(root, '64'), path.join(root, 'cache', 'gx-programs-64.bin'));
  assert.equal(portable.shaderCache(root, '32'), path.join(root, 'cache', 'gx-programs-32.bin'));
  assert.equal(portable.textureCache(root), path.join(root, 'cache', 'texture-mips'));
});

test('a sibling folder with a similar name is not treated as inside', () => {
  const sibling = `${root}-old${path.sep}sms-pc-port`;
  assert.equal(portable.storedPaths({ repo: sibling }, root).repo, sibling);
  assert.equal(portable.storedPaths({ rom: '' }, root).rom, '');
  assert.deepEqual(portable.storedPaths({ repo: 'x' }, null), { repo: 'x' });
});
