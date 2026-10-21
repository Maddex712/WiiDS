'use strict';

// Add to Steam: adds the launcher to the player's Steam library as a non-Steam
// game, with Super Mario Sunshine's artwork from SteamGridDB. Steam keeps
// non-Steam games in userdata/<account>/config/shortcuts.vdf (binary VDF) and
// their artwork in that folder's grid/, named after each shortcut's app ID.
// Steam reads shortcuts.vdf only when it starts and writes its own copy when it
// closes, so the file changes only while Steam is closed (main.js closes it).
//
// No artwork is in this repository or the launcher: it is downloaded from
// SteamGridDB when the player chooses Add to Steam, and written only into
// that player's Steam artwork folder, where Steam needs it.
const fs = require('node:fs');
const path = require('node:path');

const APP_NAME = 'Super Mario Sunshine';

// Super Mario Sunshine on SteamGridDB (https://www.steamgriddb.com/game/34899):
// its highest-scored grid, wide grid, hero, logo and icon, each saved under the
// shortcut's app ID with the suffix Steam looks for.
const ARTWORK = [
  { suffix: 'p.png', url: 'https://cdn2.steamgriddb.com/grid/f5e97c0193978d636807084bf658fc1c.png' }, // library capsule, 600x900
  { suffix: '.png', url: 'https://cdn2.steamgriddb.com/grid/cf82d2d7b18c13e95b17bc5c4c243e10.png' }, // wide capsule, 920x430
  { suffix: '_hero.png', url: 'https://cdn2.steamgriddb.com/hero/3e04f019562baeb10f41e7b9fb14c6be.png' }, // library header, 1920x620
  { suffix: '_logo.png', url: 'https://cdn2.steamgriddb.com/logo/b33128cb0089003ddfb5199e1b679652.png' }, // logo over the header
  { suffix: '_icon.png', url: 'https://cdn2.steamgriddb.com/icon/460b491b917d4185ed1f5be97229721a.png' } // icon, 256x256
];

// Binary VDF. A map is a list of [type, key, value] in file order, and values
// are kept as read (floats and 64-bit numbers as their bytes), so everything
// Steam or another tool wrote comes back out unchanged.
const MAP = 0, STRING = 1, INT32 = 2, FLOAT32 = 3, UINT64 = 7, END = 8;

function parseVdf(buffer) {
  let at = 0;
  const damaged = () => new Error("Steam's shortcuts.vdf could not be read, so it was left as it is.");
  const text = () => {
    const end = buffer.indexOf(0, at);
    if (end < 0) throw damaged();
    const value = buffer.toString('utf8', at, end);
    at = end + 1;
    return value;
  };
  const bytes = count => {
    if (at + count > buffer.length) throw damaged();
    const value = Buffer.from(buffer.subarray(at, at + count));
    at += count;
    return value;
  };
  const map = () => {
    const entries = [];
    for (;;) {
      if (at >= buffer.length) throw damaged();
      const type = buffer[at++];
      if (type === END) return entries;
      const key = text();
      if (type === MAP) entries.push([type, key, map()]);
      else if (type === STRING) entries.push([type, key, text()]);
      else if (type === INT32) entries.push([type, key, bytes(4).readInt32LE(0)]);
      else if (type === FLOAT32) entries.push([type, key, bytes(4)]);
      else if (type === UINT64) entries.push([type, key, bytes(8)]);
      else throw damaged();
    }
  };
  const root = map();
  if (at !== buffer.length) throw damaged();
  return root;
}

function serializeVdf(root) {
  const parts = [];
  const write = entries => {
    for (const [type, key, value] of entries) {
      parts.push(Buffer.from([type]), Buffer.from(`${key}\0`, 'utf8'));
      if (type === MAP) write(value);
      else if (type === STRING) parts.push(Buffer.from(`${value}\0`, 'utf8'));
      else if (type === INT32) { const b = Buffer.alloc(4); b.writeInt32LE(value | 0, 0); parts.push(b); }
      else parts.push(value);
    }
    parts.push(Buffer.from([END]));
  };
  write(root);
  return Buffer.concat(parts);
}

// Steam's keys vary in case between versions (Exe, exe), so look them up without it.
const field = (entries, key) => entries.find(entry => entry[1].toLowerCase() === key.toLowerCase());
function setField(entries, type, key, value) {
  const existing = field(entries, key);
  if (existing) { existing[0] = type; existing[2] = value; }
  else entries.push([type, key, value]);
}

const CRC_TABLE = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(text) {
  let crc = 0xffffffff;
  for (const byte of Buffer.from(text, 'utf8')) crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

// The app ID Steam and other tools give a non-Steam game: the CRC-32 of its
// quoted Exe and name, with the top bit set (no Steam app has it). Artwork is
// named after it unsigned; shortcuts.vdf stores it as a signed 32-bit number.
function shortcutAppId(quotedExe, appName) {
  return (crc32(quotedExe + appName) | 0x80000000) >>> 0;
}

const unquote = value => String(value || '').replace(/^"(.*)"$/, '$1');
function samePath(a, b, platform) {
  const normal = value => {
    const resolved = (platform === 'win32' ? path.win32 : path.posix).resolve(value);
    return platform === 'win32' ? resolved.toLowerCase() : resolved;
  };
  return normal(a) === normal(b);
}

// The shortcut in a shortcuts.vdf list that starts exe, added by this or by hand.
const findShortcut = (list, exe, platform) =>
  list.find(entry => entry[0] === MAP && field(entry[2], 'exe') && samePath(unquote(field(entry[2], 'exe')[2]), exe, platform));

// Launch options with `--play` (Skip Launcher: Steam starts the game without
// the launcher's window) added or removed, keeping the player's other options.
function withPlay(options, play) {
  const rest = options.split(/\s+/).filter(option => option && option !== '--play');
  return [...rest, ...(play ? ['--play'] : [])].join(' ');
}

// Adds the launcher to a parsed shortcuts.vdf, or finds it by its Exe when it
// is already there (added before, or by hand), keeping that entry's app ID
// and name so its play time, collections and the player's name for it stay.
// `play` turns Skip Launcher on or off; left out, the options stay as they are.
function upsertShortcut(root, { exe, startDir, appName = APP_NAME, launchOptions = '', play, platform = process.platform }) {
  let shortcuts = field(root, 'shortcuts');
  if (!shortcuts) { shortcuts = [MAP, 'shortcuts', []]; root.push(shortcuts); }
  const list = shortcuts[2];
  const quotedExe = `"${exe}"`;
  const existing = findShortcut(list, exe, platform);
  if (existing) {
    const fields = existing[2];
    const name = field(fields, 'appname')?.[2] || appName;
    if (!field(fields, 'appid')) setField(fields, INT32, 'appid', shortcutAppId(quotedExe, name) | 0);
    if (!unquote(field(fields, 'startdir')?.[2])) setField(fields, STRING, 'StartDir', `"${startDir}"`);
    if (launchOptions && !field(fields, 'launchoptions')?.[2]) setField(fields, STRING, 'LaunchOptions', launchOptions);
    if (play !== undefined) setField(fields, STRING, 'LaunchOptions', withPlay(field(fields, 'launchoptions')?.[2] || '', play));
    return { appId: field(fields, 'appid')[2] >>> 0, fields, added: false };
  }
  const appId = shortcutAppId(quotedExe, appName);
  // The fields, names and order Steam itself writes
  const fields = [
    [INT32, 'appid', appId | 0], [STRING, 'appname', appName], [STRING, 'exe', quotedExe], [STRING, 'StartDir', `"${startDir}"`],
    [STRING, 'icon', ''], [STRING, 'ShortcutPath', ''], [STRING, 'LaunchOptions', withPlay(launchOptions, play)], [INT32, 'IsHidden', 0],
    [INT32, 'AllowDesktopConfig', 1], [INT32, 'AllowOverlay', 1], [INT32, 'OpenVR', 0], [INT32, 'Devkit', 0],
    [STRING, 'DevkitGameID', ''], [INT32, 'DevkitOverrideAppID', 0], [INT32, 'LastPlayTime', 0], [STRING, 'FlatpakAppID', ''],
    [STRING, 'sortas', ''], [MAP, 'tags', []]
  ];
  const next = list.reduce((n, entry) => (/^\d+$/.test(entry[1]) ? Math.max(n, Number(entry[1]) + 1) : n), 0);
  list.push([MAP, String(next), fields]);
  return { appId, fields, added: true };
}

// Where Steam may be installed, most likely first.
function steamRoots({ platform = process.platform, home = require('node:os').homedir(), env = process.env, registryPath } = {}) {
  if (platform === 'win32')
    return [registryPath, env['ProgramFiles(x86)'] && path.win32.join(env['ProgramFiles(x86)'], 'Steam'), 'C:\\Program Files (x86)\\Steam'].filter(Boolean);
  if (platform === 'darwin') return [path.posix.join(home, 'Library/Application Support/Steam')];
  return ['.steam/steam', '.local/share/Steam', '.var/app/com.valvesoftware.Steam/.local/share/Steam', 'snap/steam/common/.local/share/Steam']
    .map(dir => path.posix.join(home, dir));
}

// Steam's install folder from `reg query HKCU\Software\Valve\Steam /v SteamPath` output.
function registrySteamPath(output) {
  const match = /SteamPath\s+REG_\w+\s+(.+)/i.exec(String(output || ''));
  return match ? path.win32.normalize(match[1].trim()) : null;
}

function findSteam(roots, files = fs) {
  return roots.find(root => { try { return files.statSync(path.join(root, 'userdata')).isDirectory(); } catch { return false; } }) || null;
}

// The Steam account to add the launcher for: the one signed in most recently,
// from config/loginusers.vdf (marked MostRecent, or else with the latest
// Timestamp, as Steam writes it now), or every account on this computer when
// that file lists none of them.
function steamAccounts(root, files = fs) {
  let ids = [];
  try { ids = files.readdirSync(path.join(root, 'userdata')).filter(name => /^\d+$/.test(name) && name !== '0'); } catch { return []; }
  let login = '';
  try { login = files.readFileSync(path.join(root, 'config', 'loginusers.vdf'), 'utf8'); } catch { /* not signed in yet */ }
  const listed = [];
  for (const [, steamId, body] of login.matchAll(/"(\d{17})"\s*\{([^}]*)\}/g)) {
    const account = String(BigInt(steamId) - 76561197960265728n);
    if (ids.includes(account))
      listed.push({ account, recent: /"MostRecent"\s*"1"/i.test(body), time: Number(/"Timestamp"\s*"(\d+)"/i.exec(body)?.[1] || 0) });
  }
  const latest = listed.find(item => item.recent) || listed.sort((a, b) => b.time - a.time)[0];
  return latest ? [latest.account] : ids;
}

// PNG files only: an error page saved as artwork would show as a broken image.
const isPng = data => Buffer.isBuffer(data) && data.length > 8 && data.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]));

// Downloads the artwork, before Steam closes. A piece that fails is left out.
async function downloadArtwork(download) {
  const results = await Promise.allSettled(ARTWORK.map(async piece => ({ ...piece, data: await download(piece.url) })));
  return results.map((result, index) => (result.status === 'fulfilled' && isPng(result.value.data) ? result.value : { ...ARTWORK[index], data: null }));
}

// Whether every account already has the launcher in its library. A file that
// cannot be read counts as not there, so Add to Steam is still offered.
function hasShortcut({ root, accounts, exe, platform = process.platform, files = fs }) {
  return accounts.length > 0 && accounts.every(account => {
    try {
      const shortcuts = field(parseVdf(files.readFileSync(path.join(root, 'userdata', account, 'config', 'shortcuts.vdf'))), 'shortcuts');
      return Boolean(shortcuts && findShortcut(shortcuts[2], exe, platform));
    } catch { return false; }
  });
}

function writeAtomically(file, data, files) {
  const temporary = `${file}.sms-launcher-new`;
  files.writeFileSync(temporary, data);
  files.renameSync(temporary, file);
}

// Adds the launcher to each account's shortcuts.vdf (backing up the old file
// first) and saves its artwork. Steam must be closed.
function addShortcut({ root, accounts, exe, startDir, appName = APP_NAME, launchOptions = '', play, artwork = [], platform = process.platform, files = fs }) {
  return accounts.map(account => {
    const config = path.join(root, 'userdata', account, 'config');
    const vdf = path.join(config, 'shortcuts.vdf');
    files.mkdirSync(path.join(config, 'grid'), { recursive: true });
    let data = null;
    try { data = files.readFileSync(vdf); } catch (error) { if (error.code !== 'ENOENT') throw error; }
    const parsed = data ? parseVdf(data) : [[MAP, 'shortcuts', []]];
    const shortcut = upsertShortcut(parsed, { exe, startDir, appName, launchOptions, play, platform });
    const saved = [];
    for (const piece of artwork) {
      if (!piece.data) continue;
      const file = path.join(config, 'grid', `${shortcut.appId}${piece.suffix}`);
      writeAtomically(file, piece.data, files);
      saved.push(piece.suffix);
      if (piece.suffix === '_icon.png') setField(shortcut.fields, STRING, 'icon', file);
    }
    if (data) files.writeFileSync(`${vdf}.sms-launcher-backup`, data);
    writeAtomically(vdf, serializeVdf(parsed), files);
    return { account, appId: shortcut.appId, added: shortcut.added, artwork: saved.length };
  });
}

module.exports = {
  APP_NAME, ARTWORK, MAP, STRING, INT32, parseVdf, serializeVdf, shortcutAppId, upsertShortcut,
  steamRoots, registrySteamPath, findSteam, steamAccounts, downloadArtwork, hasShortcut, addShortcut
};
