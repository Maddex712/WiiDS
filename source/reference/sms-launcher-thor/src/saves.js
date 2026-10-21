'use strict';

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const portable = require('./portable');

function configuredSaveDir(root, env) {
  if (env.SMS_SAVE_DIR) return env.SMS_SAVE_DIR;
  try {
    const settingsPath = env.SMS_SETTINGS ? path.resolve(root, env.SMS_SETTINGS) : path.join(root, 'settings.txt');
    const settings = fs.readFileSync(settingsPath, 'utf8');
    for (const line of settings.split(/\r?\n/)) {
      const match = line.match(/^\s*(?:save_dir|SMS_SAVE_DIR)\s*=\s*(.*?)\s*$/);
      if (match) {
        const value = match[1].replace(/\s+#.*$/, '').trim();
        if (value && !value.startsWith('#')) return value;
      }
    }
  } catch (error) { if (error.code !== 'ENOENT') throw error; }
  return '';
}

function saveDirectory(root, env = process.env, platform = process.platform) {
  const configured = configuredSaveDir(root, env);
  if (configured) return path.resolve(root, configured);
  const portableRoot = portable.root(env, platform);
  if (portableRoot) return path.join(portableRoot, 'saves', 'card-a');
  if (env.XDG_DATA_HOME) return path.resolve(env.XDG_DATA_HOME, 'sms-port', 'card-a');
  if (platform === 'win32' && env.APPDATA) return path.resolve(env.APPDATA, 'sms-port', 'card-a');
  return path.resolve(env.HOME || os.homedir(), '.local', 'share', 'sms-port', 'card-a');
}

// The game makes the card folder one '/'-separated level at a time and Windows
// mkdir fails on a missing parent, so a backslash path never gets created.
function gameSaveDirectory(dir, platform = process.platform) {
  return platform === 'win32' ? dir.replace(/\\/g, '/') : dir;
}

function prepareSaveDirectory(dir, platform = process.platform) {
  fs.mkdirSync(dir, { recursive: true });
  return gameSaveDirectory(dir, platform);
}

function backupRoot(env = process.env, platform = process.platform) {
  const portableRoot = portable.root(env, platform);
  return portableRoot ? path.join(portableRoot, 'Save Backups') : path.join(os.homedir(), 'SMS Launcher Backups');
}

function isCardFile(name) { return name === 'index.txt' || /\.(dat|stat)$/.test(name); }

function cleanupWouldRemoveSaves(root, saveDir) {
  return ['build', 'build-64', 'build-mac', 'build32'].some(name => {
    const relative = path.relative(path.join(root, name), saveDir);
    return relative === '' || (relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative));
  });
}

function saveFiles(dir) {
  if (!fs.existsSync(dir)) return [];
  if (!fs.statSync(dir).isDirectory()) throw new Error(`Memory card path is not a folder: ${dir}`);
  return fs.readdirSync(dir, { withFileTypes: true }).filter(entry => {
    if (entry.isSymbolicLink()) throw new Error(`Memory card contains a symbolic link: ${entry.name}`);
    if (!entry.isFile()) throw new Error(`Memory card contains an unexpected folder: ${entry.name}`);
    return isCardFile(entry.name);
  }).map(entry => entry.name);
}

function digest(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function backupSaves(saveDir, destination = backupRoot(), reason = 'manual') {
  const files = saveFiles(saveDir);
  if (!files.length) return { empty: true, message: 'No memory card files to back up yet.' };
  fs.mkdirSync(destination, { recursive: true, mode: 0o700 });
  const id = `${new Date().toISOString().replace(/[:.]/g, '-')}-${crypto.randomBytes(3).toString('hex')}`;
  const pending = path.join(destination, `.${id}.pending`);
  const complete = path.join(destination, id);
  fs.mkdirSync(path.join(pending, 'files'), { recursive: true, mode: 0o700 });
  try {
    const records = files.map(name => {
      const source = path.join(saveDir, name);
      const target = path.join(pending, 'files', name);
      fs.copyFileSync(source, target, fs.constants.COPYFILE_EXCL);
      const hash = digest(source);
      if (digest(target) !== hash) throw new Error(`Backup verification failed for ${name}`);
      return { name, hash };
    });
    fs.writeFileSync(path.join(pending, 'manifest.json'), JSON.stringify({ version: 1, reason,
      createdAt: new Date().toISOString(), source: saveDir, files: records }, null, 2), { flag: 'wx' });
    fs.renameSync(pending, complete);
    return { id, directory: complete, count: files.length };
  } catch (error) {
    fs.rmSync(pending, { recursive: true, force: true });
    throw error;
  }
}

function listBackups(destination = backupRoot()) {
  if (!fs.existsSync(destination)) return [];
  return fs.readdirSync(destination, { withFileTypes: true })
    .filter(entry => entry.isDirectory() && !entry.name.startsWith('.'))
    .map(entry => {
      try {
        const manifest = JSON.parse(fs.readFileSync(path.join(destination, entry.name, 'manifest.json'), 'utf8'));
        return manifest.version === 1 ? { id: entry.name, reason: manifest.reason,
          createdAt: manifest.createdAt, count: manifest.files.length, source: manifest.source } : null;
      } catch (_) { return null; }
    }).filter(Boolean).sort((a, b) => b.id.localeCompare(a.id));
}

function restoreBackup(id, saveDir, destination = backupRoot()) {
  if (!/^[0-9TZ-]+-[a-f0-9]{6}$/.test(id)) throw new Error('Invalid backup selection.');
  const folder = path.join(destination, id);
  const manifest = JSON.parse(fs.readFileSync(path.join(folder, 'manifest.json'), 'utf8'));
  if (manifest.version !== 1 || !Array.isArray(manifest.files) || !manifest.files.length)
    throw new Error('Backup manifest is invalid.');
  for (const file of manifest.files) {
    if (typeof file.name !== 'string' || file.name !== path.basename(file.name) || !isCardFile(file.name) ||
      !/^[a-f0-9]{64}$/.test(file.hash) || digest(path.join(folder, 'files', file.name)) !== file.hash)
      throw new Error(`Backup verification failed for ${file.name}.`);
  }
  const current = backupSaves(saveDir, destination, 'before-restore');
  fs.mkdirSync(saveDir, { recursive: true });
  for (const file of manifest.files) {
    const temporary = path.join(saveDir, `.${file.name}.restore-${process.pid}`);
    try {
      fs.copyFileSync(path.join(folder, 'files', file.name), temporary);
      fs.renameSync(temporary, path.join(saveDir, file.name));
    } finally { fs.rmSync(temporary, { force: true }); }
  }
  return { restored: manifest.files.length, previous: current.directory || null };
}

function moveBuildKeepingSaves(buildDirectory, destination, saveDir, backups = backupRoot()) {
  const relative = path.relative(path.resolve(buildDirectory), path.resolve(saveDir));
  const inside = relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
  const backup = inside ? backupSaves(saveDir, backups, 'before-tools-change') : null;
  fs.renameSync(buildDirectory, destination);
  try {
    if (backup && !backup.empty) restoreBackup(backup.id, saveDir, backups);
  } catch (error) {
    // Preserve any partly restored files as well as the untouched previous build.
    if (fs.existsSync(buildDirectory)) fs.renameSync(buildDirectory, `${buildDirectory}.restore-failed-${crypto.randomUUID()}`);
    fs.renameSync(destination, buildDirectory);
    throw error;
  }
  return backup;
}

module.exports = { saveDirectory, gameSaveDirectory, prepareSaveDirectory, backupRoot, cleanupWouldRemoveSaves,
  backupSaves, listBackups, restoreBackup, moveBuildKeepingSaves };
