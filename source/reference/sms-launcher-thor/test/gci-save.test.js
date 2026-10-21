'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { convertGci, readGci, importGci } = require('../src/gci-save');
const saves = require('../src/saves');

function fixture() {
  const bytes = Buffer.alloc(0x40 + 7 * 0x2000);
  bytes.write('GMSE01');
  bytes[7] = 1;
  bytes.write('super_mario_sunshine', 8);
  bytes.writeUInt32BE(0x12345678, 0x28);
  bytes.writeUInt32BE(0x44, 0x2c);
  bytes.writeUInt16BE(5, 0x30);
  bytes.writeUInt16BE(15, 0x32);
  bytes.writeUInt16BE(7, 0x38);
  bytes.writeUInt32BE(4, 0x3c);
  // Independent checksum fixture: one 0xffff word and 4093 zero words.
  // Sunshine stores the sum 0xffff unchanged; the complement sum is 0xf003.
  for (let block = 0; block < 7; block++) {
    const start = 0x40 + block * 0x2000;
    bytes.writeUInt16BE(0xffff, start);
    bytes.writeUInt16BE(0xffff, start + 0x1ffc);
    bytes.writeUInt16BE(0xf003, start + 0x1ffe);
  }
  return bytes;
}

function temporary(context) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-dolphin-import-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  return { root, card: path.join(root, 'custom card'), backups: path.join(root, 'backups') };
}

function readCard(card) {
  return Object.fromEntries(fs.readdirSync(card).map(name => [name, fs.readFileSync(path.join(card, name))]));
}

test('GCI conversion preserves every payload byte and maps big-endian metadata to host CARDStat', () => {
  const bytes = fixture();
  const result = convertGci(bytes);
  assert.deepEqual(result.data, bytes.subarray(0x40));
  assert.deepEqual(result.badBlocks, []);
  assert.equal(result.stat.length, 108);
  assert.equal(result.stat.toString('ascii', 0, 20), 'super_mario_sunshine');
  assert.equal(result.stat.readUInt32LE(0x20), 57344);
  assert.equal(result.stat.readUInt32LE(0x24), 0x12345678);
  assert.equal(result.stat.toString('ascii', 0x28, 0x2e), 'GMSE01');
  assert.equal(result.stat[0x2e], 1);
  assert.equal(result.stat.readUInt32LE(0x30), 0x44);
  assert.equal(result.stat.readUInt16LE(0x34), 5);
  assert.equal(result.stat.readUInt16LE(0x36), 15);
  assert.equal(result.stat.readUInt32LE(0x38), 4);
  bytes[0x40] = 0;
  assert.equal(result.data[0], 0xff);
});

test('corrupt block checksums warn without changing or repairing save data', () => {
  const bytes = fixture(); bytes[0x40 + 2 * 0x2000 + 10] = 1;
  const result = convertGci(bytes);
  assert.deepEqual(result.badBlocks, [3]);
  assert.deepEqual(result.data, bytes.subarray(0x40));
});

test('rejects raw cards, truncated files, foreign games/regions, unsafe names and malformed metadata', () => {
  const bytes = fixture();
  for (const input of [Buffer.alloc(0), bytes.subarray(0x40), bytes.subarray(0, bytes.length - 1),
    Buffer.concat([bytes, Buffer.alloc(1)])]) assert.throws(() => convertGci(input), /seven save blocks/);
  for (const [offset, value, message] of [[0, 'GZLE01', /North American/], [0, 'GMSP01', /North American/],
    [8, '../../outside', /not the super/]]) {
    const input = Buffer.from(bytes); input.write(value, offset);
    assert.throws(() => convertGci(input), message);
  }
  const count = Buffer.from(bytes); count.writeUInt16BE(8, 0x38);
  assert.throws(() => convertGci(count), /block count/);
  for (const offset of [0x2c, 0x3c]) {
    const input = Buffer.from(bytes); input.writeUInt32BE(0xffffffff, offset);
    assert.throws(() => convertGci(input), /metadata/);
  }
});

test('reads a .GCI file with spaces and leaves the source unchanged', context => {
  const { root } = temporary(context);
  const file = path.join(root, 'Dolphin exported save.GCI');
  const bytes = fixture(); fs.writeFileSync(file, bytes);
  assert.deepEqual(readGci(file).data, bytes.subarray(0x40));
  assert.deepEqual(fs.readFileSync(file), bytes);
  assert.throws(() => readGci(path.join(root, 'save.raw')), /\.gci/);
});

test('fresh custom card gets the payload, host metadata and a discoverable index', context => {
  const { card, backups } = temporary(context);
  const result = importGci(convertGci(fixture()), card, backups);
  assert.equal(result.previous, null);
  assert.equal(fs.readFileSync(path.join(card, 'index.txt'), 'utf8'), 'super_mario_sunshine\n');
  assert.deepEqual(fs.readFileSync(path.join(card, 'super_mario_sunshine.dat')), fixture().subarray(0x40));
  assert.equal(fs.statSync(path.join(card, 'super_mario_sunshine.stat')).size, 108);
  assert.equal(fs.readdirSync(card).length, 3);
});

test('replacement backs up all current saves, preserves other games and index slots, and is restorable', context => {
  const { card, backups } = temporary(context);
  fs.mkdirSync(card);
  fs.writeFileSync(path.join(card, 'index.txt'), 'other_game\n\nsuper_mario_sunshine\n');
  fs.writeFileSync(path.join(card, 'other_game.dat'), Buffer.from('other progress'));
  fs.writeFileSync(path.join(card, 'other_game.stat'), Buffer.alloc(108));
  fs.writeFileSync(path.join(card, 'super_mario_sunshine.dat'), 'previous Sunshine progress');
  fs.writeFileSync(path.join(card, 'super_mario_sunshine.stat'), Buffer.alloc(108, 42));
  const original = readCard(card);
  const result = importGci(convertGci(fixture()), card, backups);
  assert.ok(result.previous);
  assert.deepEqual(fs.readFileSync(path.join(card, 'other_game.dat')), original['other_game.dat']);
  assert.deepEqual(fs.readFileSync(path.join(card, 'index.txt')), original['index.txt']);
  const backup = saves.listBackups(backups)[0];
  assert.equal(backup.reason, 'before-dolphin-import');
  saves.restoreBackup(backup.id, card, backups);
  assert.deepEqual(readCard(card), original);
});

test('adds a new save into an empty index slot without moving other saves', context => {
  const { card, backups } = temporary(context); fs.mkdirSync(card);
  fs.writeFileSync(path.join(card, 'index.txt'), 'other_game\n\nsecond_game\n');
  importGci(convertGci(fixture()), card, backups);
  assert.equal(fs.readFileSync(path.join(card, 'index.txt'), 'utf8'), 'other_game\nsuper_mario_sunshine\nsecond_game\n');
});

test('backup failure prevents any card mutation', context => {
  const { card, backups } = temporary(context); fs.mkdirSync(card);
  fs.writeFileSync(path.join(card, 'super_mario_sunshine.dat'), 'current');
  fs.writeFileSync(backups, 'cannot be a backup folder');
  const original = readCard(card);
  assert.throws(() => importGci(convertGci(fixture()), card, backups));
  assert.deepEqual(readCard(card), original);
});

for (const fresh of [false, true]) test(`failed publication rolls back ${fresh ? 'fresh' : 'existing'} card`, context => {
  const { card, backups } = temporary(context); fs.mkdirSync(card);
  if (!fresh) {
    fs.writeFileSync(path.join(card, 'super_mario_sunshine.dat'), 'current');
    fs.writeFileSync(path.join(card, 'super_mario_sunshine.stat'), 'metadata');
    fs.writeFileSync(path.join(card, 'index.txt'), 'super_mario_sunshine\n');
  }
  const original = readCard(card), rename = fs.renameSync;
  context.mock.method(fs, 'renameSync', (from, to) => {
    if (String(from).includes('.dolphin-import-') && String(to).endsWith('.stat'))
      throw new Error('injected write failure');
    return rename(from, to);
  });
  assert.throws(() => importGci(convertGci(fixture()), card, backups), /injected write failure/);
  assert.deepEqual(readCard(card), original);
});

test('full or malformed cards and symlink entries are rejected without changing progress', context => {
  const { card, backups } = temporary(context); fs.mkdirSync(card);
  fs.writeFileSync(path.join(card, 'index.txt'), 'other_game\n');
  fs.writeFileSync(path.join(card, 'other_game.dat'), Buffer.alloc(53 * 0x2000));
  const original = readCard(card);
  assert.throws(() => importGci(convertGci(fixture()), card, backups), /seven free blocks/);
  assert.deepEqual(readCard(card), original);
  fs.writeFileSync(path.join(card, 'index.txt'), '../outside\n');
  assert.throws(() => importGci(convertGci(fixture()), card, backups), /index is invalid/);
  fs.writeFileSync(path.join(card, 'index.txt'), Array.from({ length: 127 }, (_, i) => `file${i}`).join('\n'));
  assert.throws(() => importGci(convertGci(fixture()), card, backups), /no free file slots/);
  if (process.platform !== 'win32') {
    fs.symlinkSync(path.join(card, 'other_game.dat'), path.join(card, 'link.dat'));
    assert.throws(() => importGci(convertGci(fixture()), card, backups), /symbolic link/);
  }
});
