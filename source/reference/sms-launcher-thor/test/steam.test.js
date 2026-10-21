'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const steam = require('../src/steam');
const { MAP, STRING, INT32 } = steam;

const field = (entries, key) => entries.find(entry => entry[1].toLowerCase() === key.toLowerCase())?.[2];
const PNG = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 1, 2, 3]);

// A shortcuts.vdf as Steam writes it, with one game added by hand and a float
// and a 64-bit value from some other tool.
function steamFile() {
  const other = [[INT32, 'appid', -1234567], [STRING, 'appname', 'Some Game'], [STRING, 'exe', '"C:\\Games\\some.exe"'],
    [STRING, 'StartDir', '"C:\\Games\\"'], [INT32, 'LastPlayTime', 1700000000], [3, 'scale', Buffer.from([0, 0, 128, 63])],
    [7, 'big', Buffer.from([1, 2, 3, 4, 5, 6, 7, 8])], [MAP, 'tags', [[STRING, '0', 'Favorites']]]];
  return steam.serializeVdf([[MAP, 'shortcuts', [[MAP, '0', other]]]]);
}

test('shortcuts.vdf reads and writes back byte for byte', () => {
  const data = steamFile();
  assert.deepEqual(steam.serializeVdf(steam.parseVdf(data)), data);
  assert.equal(data.at(-1), 8);
  assert.equal(data.at(-2), 8);
});

test('a damaged shortcuts.vdf is refused', () => {
  const data = steamFile();
  for (const broken of [data.subarray(0, data.length - 3), Buffer.concat([data, Buffer.from([1])]), Buffer.from([9, 65, 0, 8])])
    assert.throws(() => steam.parseVdf(broken), /could not be read/);
});

test('app IDs are the CRC-32 of the quoted Exe and name, with the top bit set', () => {
  assert.equal(steam.shortcutAppId('1234', '56789'), 0xcbf43926); // CRC-32 of "123456789"
  const id = steam.shortcutAppId('"C:\\SMS Launcher\\SMS Launcher.exe"', 'Super Mario Sunshine');
  assert.ok(id >= 0x80000000 && id <= 0xffffffff);
  assert.equal(id, steam.shortcutAppId('"C:\\SMS Launcher\\SMS Launcher.exe"', 'Super Mario Sunshine'));
});

test('the launcher is added after the other shortcuts, as Steam writes them', () => {
  const root = steam.parseVdf(steamFile());
  const result = steam.upsertShortcut(root, { exe: 'C:\\Programs\\SMS Launcher\\SMS Launcher.exe', startDir: 'C:\\Programs\\SMS Launcher', platform: 'win32' });
  const list = field(root, 'shortcuts');
  assert.equal(result.added, true);
  assert.deepEqual(list.map(entry => entry[1]), ['0', '1']);
  assert.equal(field(list[0][2], 'appname'), 'Some Game');
  const fields = list[1][2];
  assert.equal(field(fields, 'appname'), 'Super Mario Sunshine');
  assert.equal(field(fields, 'exe'), '"C:\\Programs\\SMS Launcher\\SMS Launcher.exe"');
  assert.equal(field(fields, 'StartDir'), '"C:\\Programs\\SMS Launcher"');
  assert.equal(field(fields, 'appid') >>> 0, result.appId);
  assert.equal(field(fields, 'AllowOverlay'), 1);
});

test('a launcher already in Steam is updated in place, keeping its app ID and name', () => {
  const root = steam.parseVdf(steamFile());
  field(root, 'shortcuts').push([MAP, '1', [[INT32, 'appid', -42], [STRING, 'appname', 'Mario'], [STRING, 'exe', '"C:\\PROGRAMS\\SMS Launcher\\SMS Launcher.exe"'], [STRING, 'LaunchOptions', '']]]);
  const result = steam.upsertShortcut(root, { exe: 'C:\\Programs\\SMS Launcher\\SMS Launcher.exe', startDir: 'C:\\Programs\\SMS Launcher', platform: 'win32' });
  assert.equal(result.added, false);
  assert.equal(result.appId, -42 >>> 0);
  assert.equal(field(root, 'shortcuts').length, 2);
  assert.equal(field(result.fields, 'appname'), 'Mario');
  assert.equal(field(result.fields, 'StartDir'), '"C:\\Programs\\SMS Launcher"');
});

test('on Linux, paths match exactly and the AppImage gets its launch options', () => {
  const root = [[MAP, 'shortcuts', [[MAP, '0', [[INT32, 'appid', -7], [STRING, 'exe', '"/home/deck/Apps/sms-launcher.appimage"']]]]]];
  const result = steam.upsertShortcut(root, { exe: '/home/deck/Apps/SMS-Launcher.AppImage', startDir: '/home/deck/Apps', launchOptions: '--no-sandbox', platform: 'linux' });
  assert.equal(result.added, true);
  assert.equal(field(result.fields, 'LaunchOptions'), '--no-sandbox');
});

test('Skip Launcher adds --play to the launch options, and turning it off removes it', () => {
  const root = [[MAP, 'shortcuts', []]];
  const options = { exe: '/home/deck/Apps/SMS-Launcher.AppImage', startDir: '/home/deck/Apps', launchOptions: '--no-sandbox', platform: 'linux' };
  const added = steam.upsertShortcut(root, { ...options, play: true });
  assert.equal(field(added.fields, 'LaunchOptions'), '--no-sandbox --play');
  // the player's own options stay, and --play is never doubled
  added.fields.find(entry => entry[1] === 'LaunchOptions')[2] = '--no-sandbox --play -fullscreen';
  assert.equal(field(steam.upsertShortcut(root, { ...options, play: true }).fields, 'LaunchOptions'), '--no-sandbox -fullscreen --play');
  assert.equal(field(steam.upsertShortcut(root, { ...options, play: false }).fields, 'LaunchOptions'), '--no-sandbox -fullscreen');
  assert.equal(field(steam.upsertShortcut(root, options).fields, 'LaunchOptions'), '--no-sandbox -fullscreen');
  assert.equal(field(root, 'shortcuts').length, 1);
});

test('Steam is looked for where each system installs it', () => {
  const linux = steam.steamRoots({ platform: 'linux', home: '/home/deck' });
  assert.ok(linux.includes('/home/deck/.local/share/Steam'));
  assert.ok(linux.includes('/home/deck/.var/app/com.valvesoftware.Steam/.local/share/Steam'));
  const windows = steam.steamRoots({ platform: 'win32', env: {}, registryPath: 'D:\\Steam' });
  assert.equal(windows[0], 'D:\\Steam');
  assert.equal(steam.registrySteamPath('\r\nHKEY_CURRENT_USER\\Software\\Valve\\Steam\r\n    SteamPath    REG_SZ    c:/program files (x86)/steam\r\n'),
    'c:\\program files (x86)\\steam');
  assert.equal(steam.registrySteamPath('ERROR: The system was unable to find the specified registry key'), null);
});

function fakeSteam(t, { accounts = ['111', '222'], login = '' } = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'steam-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  for (const account of [...accounts, '0']) fs.mkdirSync(path.join(root, 'userdata', account, 'config'), { recursive: true });
  fs.mkdirSync(path.join(root, 'config'), { recursive: true });
  if (login) fs.writeFileSync(path.join(root, 'config', 'loginusers.vdf'), login);
  return root;
}

test('the launcher goes to the account signed in most recently, or to every account', t => {
  const login = '"users"\n{\n\t"76561197960265839"\n\t{\n\t\t"AccountName"\t"a"\n\t\t"MostRecent"\t\t"0"\n\t}\n' +
    '\t"76561197960265950"\n\t{\n\t\t"AccountName"\t"b"\n\t\t"MostRecent"\t\t"1"\n\t}\n}\n';
  assert.deepEqual(steam.steamAccounts(fakeSteam(t, { login })), ['222']);
  // Steam now writes no MostRecent: the latest Timestamp, and accounts it no longer lists are left alone
  const timestamps = '"users"\n{\n\t"76561197960265839"\n\t{\n\t\t"Timestamp"\t\t"1700000000"\n\t}\n' +
    '\t"76561197960265950"\n\t{\n\t\t"Timestamp"\t\t"1790000000"\n\t}\n}\n';
  assert.deepEqual(steam.steamAccounts(fakeSteam(t, { login: timestamps })), ['222']);
  const onlyOne = '"users"\n{\n\t"76561197960265839"\n\t{\n\t\t"AutoLogin"\t\t"1"\n\t\t"Timestamp"\t\t"1790000000"\n\t}\n}\n';
  assert.deepEqual(steam.steamAccounts(fakeSteam(t, { login: onlyOne })), ['111']);
  assert.deepEqual(steam.steamAccounts(fakeSteam(t)).sort(), ['111', '222']);
  assert.equal(steam.findSteam(['/nowhere', fakeSteam(t)]) !== null, true);
  assert.equal(steam.findSteam(['/nowhere']), null);
});

test('adding to Steam writes the shortcut and artwork, and backs up the old file', t => {
  const root = fakeSteam(t, { accounts: ['222'] });
  const config = path.join(root, 'userdata', '222', 'config');
  const old = steamFile();
  fs.writeFileSync(path.join(config, 'shortcuts.vdf'), old);
  const artwork = steam.ARTWORK.map((piece, index) => ({ ...piece, data: index === 1 ? null : PNG }));
  const options = { root, accounts: ['222'], exe: 'C:\\SMS\\SMS Launcher.exe', startDir: 'C:\\SMS', artwork, platform: 'win32' };
  const [first] = steam.addShortcut(options);
  assert.equal(first.added, true);
  assert.equal(first.artwork, 4);
  assert.deepEqual(fs.readFileSync(path.join(config, 'shortcuts.vdf.sms-launcher-backup')), old);
  const grid = fs.readdirSync(path.join(config, 'grid')).sort();
  assert.deepEqual(grid, [`${first.appId}_hero.png`, `${first.appId}_icon.png`, `${first.appId}_logo.png`, `${first.appId}p.png`]);
  const list = field(steam.parseVdf(fs.readFileSync(path.join(config, 'shortcuts.vdf'))), 'shortcuts');
  assert.equal(list.length, 2);
  assert.equal(field(list[1][2], 'icon'), path.join(config, 'grid', `${first.appId}_icon.png`));
  // again: the same shortcut, no copy
  const [second] = steam.addShortcut(options);
  assert.equal(second.added, false);
  assert.equal(second.appId, first.appId);
  assert.equal(field(steam.parseVdf(fs.readFileSync(path.join(config, 'shortcuts.vdf'))), 'shortcuts').length, 2);
});

test('an account without shortcuts.vdf gets a new one', t => {
  const root = fakeSteam(t, { accounts: ['111'] });
  const [result] = steam.addShortcut({ root, accounts: ['111'], exe: '/opt/SMS-Launcher.AppImage', startDir: '/opt', platform: 'linux' });
  const list = field(steam.parseVdf(fs.readFileSync(path.join(root, 'userdata', '111', 'config', 'shortcuts.vdf'))), 'shortcuts');
  assert.equal(list.length, 1);
  assert.equal(field(list[0][2], 'appid') >>> 0, result.appId);
  assert.equal(fs.existsSync(path.join(root, 'userdata', '111', 'config', 'shortcuts.vdf.sms-launcher-backup')), false);
});

test('the banner knows when every account already has the launcher', t => {
  const root = fakeSteam(t, { accounts: ['111', '222'] });
  const options = { root, accounts: ['111', '222'], exe: 'C:\\SMS\\SMS Launcher.exe', startDir: 'C:\\SMS', platform: 'win32' };
  assert.equal(steam.hasShortcut(options), false);
  steam.addShortcut({ ...options, accounts: ['111'] });
  assert.equal(steam.hasShortcut(options), false);
  steam.addShortcut(options);
  assert.equal(steam.hasShortcut(options), true);
  assert.equal(steam.hasShortcut({ ...options, exe: 'C:\\Other\\SMS Launcher.exe' }), false);
  fs.writeFileSync(path.join(root, 'userdata', '222', 'config', 'shortcuts.vdf'), 'broken');
  assert.equal(steam.hasShortcut(options), false);
  assert.equal(steam.hasShortcut({ ...options, accounts: [] }), false);
});

test('artwork that fails to download, or is not a PNG, is left out', async () => {
  const artwork = await steam.downloadArtwork(async url => {
    if (url.includes('/hero/')) throw new Error('offline');
    if (url.includes('/logo/')) return Buffer.from('<html>not found</html>');
    return PNG;
  });
  assert.deepEqual(artwork.map(piece => Boolean(piece.data)), [true, true, false, false, true]);
});
