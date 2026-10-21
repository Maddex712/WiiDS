'use strict';

const fs = require('node:fs');
const path = require('node:path');
const saves = require('./saves');

const HEADER = 0x40;
const BLOCK = 0x2000;
const NAME = 'super_mario_sunshine';
const SIZE = 7 * BLOCK;

// GCI is a big-endian GameCube directory entry followed by the unchanged
// card file data. CARDStat is the port's 108-byte, little-endian host struct.
// Format references: Dolphin's GCMemcard.h and sms-pc-port/platform/card/card.cpp.
function convertGci(bytes) {
  if (!Buffer.isBuffer(bytes) || bytes.length !== HEADER + SIZE)
    throw new Error('Choose a Super Mario Sunshine .gci export (64-byte header and seven save blocks).');
  if (!bytes.subarray(0, 6).equals(Buffer.from('GMSE01')))
    throw new Error('This importer supports North American Super Mario Sunshine saves (GMSE01). Export the matching save from Dolphin.');
  const filename = bytes.subarray(8, 40);
  const end = filename.indexOf(0);
  if (!filename.subarray(0, end < 0 ? 32 : end).equals(Buffer.from(NAME)))
    throw new Error('This is not the super_mario_sunshine save. Choose the Sunshine game save exported from Dolphin.');
  if (bytes.readUInt16BE(0x38) !== 7)
    throw new Error('The GCI block count does not match its save data. Export the save again from Dolphin.');
  const icon = bytes.readUInt32BE(0x2c), comment = bytes.readUInt32BE(0x3c);
  if (icon === 0xffffffff || icon >= BLOCK || comment === 0xffffffff || comment > BLOCK - 64)
    throw new Error('The GCI banner or comment metadata is invalid. Export the save again from Dolphin.');

  const data = Buffer.from(bytes.subarray(HEADER));
  const badBlocks = [];
  for (let block = 0; block < 7; block++) {
    const start = block * BLOCK;
    let sum = 0, inverted = 0;
    for (let offset = 0; offset < BLOCK - 4; offset += 2) {
      const word = data.readUInt16BE(start + offset);
      sum = (sum + word) & 0xffff;
      inverted = (inverted + (word ^ 0xffff)) & 0xffff;
    }
    // Sunshine does not normalize 0xffff to zero (unlike card filesystem checksums).
    if (data.readUInt16BE(start + BLOCK - 4) !== sum ||
        data.readUInt16BE(start + BLOCK - 2) !== inverted) badBlocks.push(block + 1);
  }
  const stat = Buffer.alloc(0x6c);
  stat.write(NAME, 0);
  stat.writeUInt32LE(data.length, 0x20);
  stat.writeUInt32LE(bytes.readUInt32BE(0x28), 0x24);
  bytes.copy(stat, 0x28, 0, 6);
  stat[0x2e] = bytes[7];
  stat.writeUInt32LE(icon, 0x30);
  stat.writeUInt16LE(bytes.readUInt16BE(0x30), 0x34);
  stat.writeUInt16LE(bytes.readUInt16BE(0x32), 0x36);
  stat.writeUInt32LE(comment, 0x38);
  // The host card backend leaves derived icon offsets zero when creating saves.
  return { name: NAME, data, stat, badBlocks };
}

function readGci(file) {
  if (typeof file !== 'string' || path.extname(file).toLowerCase() !== '.gci')
    throw new Error('Choose a Dolphin .gci save file.');
  const fd = fs.openSync(file, 'r');
  try {
    if (!fs.fstatSync(fd).isFile()) throw new Error('Choose a Dolphin .gci save file.');
    const bytes = Buffer.alloc(HEADER + SIZE + 1);
    let length = 0, received;
    while (length < bytes.length && (received = fs.readSync(fd, bytes, length, bytes.length - length, null)))
      length += received;
    return convertGci(bytes.subarray(0, length));
  } finally { fs.closeSync(fd); }
}

function importGci(converted, saveDir, backups = saves.backupRoot()) {
  // Complete all validation and a verified backup before changing the card.
  const previous = saves.backupSaves(saveDir, backups, 'before-dolphin-import');
  const indexFile = path.join(saveDir, 'index.txt');
  const index = fs.existsSync(indexFile) ? fs.readFileSync(indexFile, 'utf8') : '';
  const entries = index ? index.split('\n') : [];
  while (entries.length && entries.at(-1) === '') entries.pop();
  if (entries.length > 127 || entries.some(name => name && !/^[A-Za-z0-9_.-]+$/.test(name)))
    throw new Error('The memory card index is invalid. Your current saves have been kept.');
  if (!entries.includes(NAME)) {
    const empty = entries.indexOf('');
    if (empty >= 0) entries[empty] = NAME;
    else {
      if (entries.length >= 127) throw new Error('The memory card has no free file slots.');
      entries.push(NAME);
    }
  }
  const blocks = entries.filter(name => name && name !== NAME).reduce((total, name) => {
    const file = path.join(saveDir, `${name}.dat`);
    return total + (fs.existsSync(file) ? Math.ceil(fs.statSync(file).size / BLOCK) : 0);
  }, 7);
  if (blocks > 59) throw new Error('The memory card needs seven free blocks for this Sunshine save.');
  const files = new Map([[`${NAME}.dat`, converted.data], [`${NAME}.stat`, converted.stat],
    ['index.txt', Buffer.from(`${entries.join('\n')}\n`)]]);
  fs.mkdirSync(saveDir, { recursive: true });
  const staging = fs.mkdtempSync(path.join(saveDir, '.dolphin-import-'));
  const replaced = [];
  const originals = new Map();
  try {
    for (const [name, bytes] of files) {
      const target = path.join(saveDir, name);
      originals.set(name, fs.existsSync(target) ? fs.readFileSync(target) : null);
      fs.writeFileSync(path.join(staging, name), bytes, { flag: 'wx', mode: 0o600 });
    }
    // Publish the index last, after both files are ready for the card backend.
    for (const name of files.keys()) {
      fs.renameSync(path.join(staging, name), path.join(saveDir, name));
      replaced.push(name);
    }
  } catch (error) {
    try {
      for (const name of replaced.reverse()) {
        const original = originals.get(name), target = path.join(saveDir, name);
        if (original === null) fs.rmSync(target);
        else fs.writeFileSync(target, original);
      }
    } catch (rollback) {
      throw new Error(`Import failed and could not restore every file: ${rollback.message}. Your backup is at ${previous.directory || backups}.`, { cause: error });
    }
    throw error;
  } finally { fs.rmSync(staging, { recursive: true, force: true }); }
  return { name: NAME, directory: saveDir, previous: previous.directory || null };
}

module.exports = { convertGci, readGci, importGci };
