'use strict';

const { app, BrowserWindow, ipcMain, dialog, shell, clipboard, net, screen } = require('electron');
const fs = require('node:fs');
const path = require('node:path');
const { spawn, execFile } = require('node:child_process');
const crypto = require('node:crypto');
const port = require('./port');
const saves = require('./saves');
const gciSave = require('./gci-save');
const buildTools = require('./build-tools');
const game = require('./game-version');
const updateChannel = require('./update-channel');
const steam = require('./steam');
const appImageUpdate = require('./appimage-update');
const bindings = require('./bindings');
const prompts = require('./prompts');
const gameSource = require('./game-source');
const portable = require('./portable');
const telemetry = require('./telemetry');
const discord = require('./discord');
const presence = require('./presence');
const changelog = require('./changelog');
const { cleanOutputLine, createActivityReader } = require('./progress');
const { createActivityLog } = require('./activity-log');
const { observeProcess, exitDescription, failureMessage } = require('./process-output');
const { createSessionLog } = require('./session-log');
const displayScale = require('./display-scale');

const DISCORD_INVITE = 'https://discord.gg/NvUXmm8dB9';
// Microsoft's free Windows HDR Calibration app (Microsoft Store product 9N7F2SM5D1LR).
const HDR_CALIBRATION_APP = 'https://apps.microsoft.com/detail/9n7f2sm5d1lr';

let window;
let config;
let active = null;
let preparingTools = false;
let operation = null;
let appUpdate = { state: 'idle', message: '' };
let updater = null;
// Before anything reads userData: the single-instance lock is keyed on it too,
// so each portable folder runs independently of an installed launcher.
const portableRoot = portable.root();
if (portableRoot) {
  fs.mkdirSync(portableRoot, { recursive: true });
  app.setPath('userData', portableRoot);
  app.setPath('sessionData', path.join(portableRoot, 'Electron'));
}
const activityLog = createActivityLog();
const partialOutput = new Map();
const sessionLog = createSessionLog(() => app.getPath('userData'), error =>
  logOutput(`Could not save the session log: ${error.message}`, 'launcher'));
let reporter = null;
let online = null;
let discordPresence = null;
// The running game's handler for its `[presence]` lines (src/presence.js).
let onPresence = null;
// `--play` (for a Steam shortcut's launch options) starts the game without the
// launcher's window and quits when the game closes.
const playOnStart = process.argv.includes('--play');
const ownsInstance = app.requestSingleInstanceLock();
if (!ownsInstance) app.quit();
app.on('second-instance', () => {
  if (!window) return;
  if (window.isMinimized()) window.restore();
  window.show();
  window.focus();
});

function broadcast(channel, data) {
  if (window && !window.isDestroyed()) window.webContents.send(channel, data);
}

function activityState() {
  const task = active || operation;
  return task ? { label: task.label, detail: task.detail, percent: task.percent,
    startedAt: task.startedAt, canStop: Boolean(active?.child) } : null;
}

function log(message) {
  const lines = String(message).replace(/\r/g, '').split('\n');
  for (const line of lines) {
    if (!line) continue;
    logOutput(cleanOutputLine(line), 'launcher');
    sessionLog.write(cleanOutputLine(line), 'launcher');
  }
}

function logOutput(text, stream, previous) {
  const entry = activityLog.write(text, stream, previous);
  if (stream !== 'launcher') partialOutput.set(entry.id, entry);
  broadcast('log', entry);
  return entry;
}

function completeOutput(text, stream, entry) {
  sessionLog.write(text, stream);
  if (entry) partialOutput.delete(entry.id);
}

function flushPartialOutput() {
  for (const entry of partialOutput.values()) sessionLog.write(entry.text, entry.stream);
  partialOutput.clear();
}

// Monitoring preserves Node's normal fatal-error behavior while retaining the
// diagnostic on disk, even when the launcher cannot keep its window alive.
process.on('uncaughtExceptionMonitor', error => {
  log(`Uncaught launcher error: ${error.stack || error.message}`);
  flushPartialOutput();
});
process.on('warning', warning => log(`Launcher warning: ${warning.stack || warning.message}`));
app.on('render-process-gone', (_event, _contents, details) => {
  log(`Launcher renderer ${details.reason}: ${exitDescription('Renderer', details.exitCode, null)}`);
});
app.on('child-process-gone', (_event, details) => {
  log(`Launcher ${details.type} process ${details.reason}: ${exitDescription(details.name || details.type, details.exitCode, null)}`);
});

// A game source chosen under Settings, with the commits it resolved to.
function savedGameSource(saved) {
  try {
    const override = gameSource.normalizeOverride(saved?.override);
    const resolved = saved?.resolved;
    if (!override || !/^[a-f0-9]{40}$/.test(resolved?.commit) || !/^[a-f0-9]{40}$/.test(resolved?.decomp)) return null;
    return { override, resolved: { ...resolved, custom: true, override } };
  } catch (_) { return null; }
}

function currentSource() { return config.gameSource?.resolved || game.release; }

async function chooseGameSource(input) {
  await ensureBuildTools();
  const override = gameSource.normalizeOverride(input);
  if (!override) {
    config.gameSource = null;
    saveConfig();
    log(`Game source: this launcher release (${game.release.version}). Update game to switch back.`);
    return state();
  }
  const resolved = await gameSource.resolve(override, { capture, env: toolEnv() });
  config.gameSource = { override, resolved };
  saveConfig();
  log(`Game source: ${resolved.repository} at ${resolved.commit.slice(0, 12)}, decomp ${resolved.decompRepository || 'pinned by it'} at ${resolved.decomp.slice(0, 12)}. Update game to build it.`);
  return state();
}

// Update game follows a chosen branch to its newest commit.
async function refreshGameSource() {
  if (!config.gameSource) return;
  try {
    const resolved = await gameSource.resolve(config.gameSource.override, { capture, env: toolEnv() });
    if (resolved.commit !== config.gameSource.resolved.commit || resolved.decomp !== config.gameSource.resolved.decomp) {
      config.gameSource = { ...config.gameSource, resolved };
      saveConfig();
      log(`Game source moved to ${resolved.version}.`);
    }
  } catch (error) { log(`Could not check the chosen game source for changes: ${error.message}`); }
}

function configFile() { return path.join(app.getPath('userData'), 'preferences.json'); }

function defaultRepo() {
  const sibling = path.resolve(__dirname, '..', '..', 'sms-port');
  return !app.isPackaged && port.isPort(sibling)
    ? sibling : path.join(app.getPath('userData'), 'sms-pc-port');
}

function saveConfig() {
  const file = configFile();
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const temporary = `${file}.tmp`;
  fs.writeFileSync(temporary, JSON.stringify(portable.storedPaths(config, portableRoot), null, 2), { mode: 0o600 });
  fs.renameSync(temporary, file);
}

function loadConfig() {
  let saved = {};
  try { saved = portable.resolvedPaths(JSON.parse(fs.readFileSync(configFile(), 'utf8')), portableRoot); } catch (_) { /* first run */ }
  config = {
    repo: typeof saved.repo === 'string' ? saved.repo : defaultRepo(),
    rom: typeof saved.rom === 'string' ? saved.rom : '',
    settings: port.normalizeSettings(port.savedSettings(saved.settings, typeof saved.repo === 'string' ? saved.repo : defaultRepo())),
    installRoot: typeof saved.installRoot === 'string' ? saved.installRoot : null,
    saveDirectory: typeof saved.saveDirectory === 'string' ? saved.saveDirectory : null,
    previousInstall: saved.previousInstall || null,
    completedSetup: Boolean(saved.completedSetup),
    gameSource: savedGameSource(saved.gameSource),
    // Random, made on first run, and only ever sent with usage heartbeats.
    installId: telemetry.validInstallId(saved.installId) ? saved.installId : crypto.randomUUID(),
    // The newest launcher version whose changelog the player has been shown.
    changelogSeen: changelog.baseVersion(saved.changelogSeen),
    // Which wording of Beta's notes a Beta build last showed.
    changelogBetaSeen: /^[a-f0-9]{16}$/.test(saved.changelogBetaSeen) ? saved.changelogBetaSeen : null
  };
  if (config.installId !== saved.installId) {
    try { saveConfig(); } catch (error) { log(`Could not save preferences: ${error.message}`); }
  }
  for (const root of [config.repo, config.previousInstall?.repo].filter(Boolean))
    for (const arch of port.platformInfo().arches)
      for (const eclipse of [false, true]) {
        try { if (game.recoverBuild(root, { arch, eclipse })) log('Recovered your previous game after interrupted setup.'); }
        catch (error) { log(`Game recovery: ${error.message}`); }
      }
  config.completedSetup ||= fs.existsSync(port.binaryPath(config.repo, config.settings));
}

async function exclusive(label, callback, newLog = false) {
  if (operation || active || preparingTools) throw new Error('Wait for the current task to finish, or stop it first.');
  if (newLog) {
    activityLog.reset();
    partialOutput.clear();
    broadcast('log-reset', activityLog.resetSequence());
    sessionLog.reset();
  }
  operation = { label, detail: 'Starting…', percent: null, startedAt: Date.now() };
  broadcast('activity', operation);
  try { return await callback(); }
  catch (error) {
    if (!error.activityLogged) {
      log(`✕ ${label}: ${error.message}`);
      error.activityLogged = true;
    }
    throw error;
  }
  finally { operation = null; broadcast('activity', null); }
}

// Add to Steam (steam.js). Steam reads non-Steam games only when it starts and
// writes them back when it closes, so an open Steam closes first (steam://exit)
// and opens again afterwards.
function steamRunning() {
  const [command, args] = process.platform === 'win32' ? ['tasklist', ['/FI', 'IMAGENAME eq steam.exe', '/NH']] : ['pgrep', ['-x', 'steam']];
  return new Promise(resolve => execFile(command, args, { windowsHide: true },
    (error, stdout) => resolve(process.platform === 'win32' ? /steam\.exe/i.test(stdout || '') : !error)));
}

function windowsSteamPath() {
  return new Promise(resolve => execFile('reg', ['query', 'HKCU\\Software\\Valve\\Steam', '/v', 'SteamPath'], { windowsHide: true },
    (error, stdout) => resolve(error ? null : steam.registrySteamPath(stdout))));
}

// Steam artwork comes straight from SteamGridDB into memory: no-store keeps it
// out of the launcher's web cache, so the only copy is the one in Steam's folder.
async function downloadImage(url) {
  const response = await net.fetch(url, { cache: 'no-store' });
  if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
  return Buffer.from(await response.arrayBuffer());
}

// The file Steam starts: the installed launcher, or the AppImage on Linux.
// Not in development, and not on macOS.
function launcherExe() {
  if (!app.isPackaged || !['win32', 'linux'].includes(process.platform)) return null;
  return (process.platform === 'linux' ? process.env.APPIMAGE : process.execPath) || null;
}

const startedFromSteam = () => Boolean(process.env.SteamGameId || process.env.SteamAppId);

async function locateSteam() {
  const registryPath = process.platform === 'win32' ? await windowsSteamPath() : null;
  const root = steam.findSteam(steam.steamRoots({ registryPath }));
  return root ? { root, accounts: steam.steamAccounts(root) } : null;
}

// For the home screen's Add to Steam banner: offered while Steam is installed
// and the launcher is not in its library yet.
async function steamStatus() {
  const exe = launcherExe();
  if (!exe || startedFromSteam()) return { available: false };
  const located = await locateSteam();
  if (!located || !located.accounts.length) return { available: false };
  return { available: true, inSteam: steam.hasShortcut({ ...located, exe }) };
}

// `play` (Skip Launcher) adds `--play`, so Steam starts the game without the launcher's window.
async function addToSteam({ play = false } = {}) {
  const exe = launcherExe();
  if (!exe) throw new Error(process.platform === 'linux' ? 'Add to Steam works from the launcher AppImage.' : 'Add to Steam works from the installed launcher on Windows and Linux.');
  if (startedFromSteam()) throw new Error('The launcher was started from Steam, so it is already in your Steam library.');
  const located = await locateSteam();
  if (!located) throw new Error("Steam isn't installed on this computer, or nobody has signed in to it yet.");
  const { root, accounts } = located;
  if (!accounts.length) throw new Error('Sign in to Steam once, then try again.');
  log('Downloading Steam artwork from SteamGridDB…');
  const artwork = await steam.downloadArtwork(downloadImage);
  const wasRunning = await steamRunning();
  if (wasRunning) {
    const answer = await dialog.showMessageBox(window, {
      type: 'question', buttons: ['Cancel', 'Close Steam and add'], defaultId: 1, cancelId: 0,
      message: 'Close Steam to add the launcher?',
      detail: 'Steam picks up new non-Steam games only when it starts, so it closes for a moment and opens again afterwards.'
    });
    if (answer.response !== 1) return { cancelled: true };
    log('Closing Steam…');
    await shell.openExternal('steam://exit');
    const deadline = Date.now() + 60000;
    while (await steamRunning()) {
      if (Date.now() > deadline) throw new Error("Steam didn't close. Close Steam yourself, then choose Add to Steam again.");
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
    await new Promise(resolve => setTimeout(resolve, 1500)); // Steam's last writes
  }
  const results = steam.addShortcut({ root, accounts, exe, startDir: path.dirname(exe), artwork,
    launchOptions: process.platform === 'linux' ? '--no-sandbox' : '', play: Boolean(play) });
  const added = results.some(result => result.added);
  const images = Math.min(...results.map(result => result.artwork));
  log(`${added ? 'Added' : 'Updated'} ${steam.APP_NAME} in Steam${play ? ' with Skip Launcher' : ''} (${images} of ${steam.ARTWORK.length} artwork images, ${root}).`);
  if (wasRunning) shell.openExternal('steam://open/games').catch(() => {});
  return { added, play: Boolean(play), name: steam.APP_NAME, artwork: images, total: steam.ARTWORK.length, reopened: wasRunning };
}

function requireRepo() {
  if (!port.isPort(config.repo)) throw new Error('Download the setup files or choose a folder that already has them.');
  return config.repo;
}

function currentSaveDirectory() {
  return process.env.SMS_SAVE_DIR ? saves.saveDirectory(config.repo) : config.saveDirectory || saves.saveDirectory(config.repo);
}

function makeSaveBackup(reason) {
  if (active) throw new Error('Stop the running game or task before backing up saves.');
  const result = saves.backupSaves(currentSaveDirectory(), saves.backupRoot(), reason);
  log(result.empty ? result.message : `Saved ${result.count} memory card file(s) to ${result.directory}`);
  return result;
}

async function launch(command, args, options = {}, label = 'Task', isGame = false) {
  if (active) throw new Error(`Wait for ${active.label} to finish, or stop it first.`);
  try {
    log(`▶ ${label}`);
    // Windows applies windowsHide's "start hidden" to a program's first window,
    // so the game would run with an invisible window. Detached instead, the game
    // (a console program) gets no console window and shows its own normally.
    const windowsGame = isGame && process.platform === 'win32';
    const child = spawn(command, args, { ...options, detached: process.platform !== 'win32' || windowsGame,
      env: { ...(options.env || process.env), PYTHONUNBUFFERED: '1' },
      windowsHide: !windowsGame, stdio: ['ignore', 'pipe', 'pipe'] });
    active = { label, child, detail: label.includes('build tools') ? 'Downloading and preparing tools…' : 'Starting…',
      percent: null, startedAt: Date.now() };
    broadcast('activity', activityState());
    const recent = [], readActivity = createActivityReader();
    // The game's [presence] lines go to Discord, not the activity log; a
    // partial line that could become one waits for the rest.
    function output(text, stream, entry) {
      if (isGame && stream === 'stdout' && !entry && text
        && (presence.isPresenceLine(text) || presence.PREFIX.startsWith(text))) return entry;
      return logOutput(text, stream, entry);
    }
    function line(value, stream, entry) {
      if (isGame && stream === 'stdout' && presence.isPresenceLine(value)) return onPresence?.(value);
      completeOutput(value, stream, entry);
      if (!value) return;
      if (recent.push(value) > 80) recent.shift();
      const progress = isGame ? null : readActivity(value);
      if (progress && active && active.child === child) {
        Object.assign(active, progress);
        broadcast('activity', activityState());
      }
    }
    const { error, code, signal } = await observeProcess(child, { output, line });
    active = null;
    broadcast('activity', activityState());
    if (error) throw error;
    if (code !== 0 || signal) {
      throw new Error(failureMessage(label, { code, signal }, recent, { game: isGame }));
    }
    log(`✓ ${label} finished`);
    return { ok: true };
  } catch (error) {
    log(`✕ ${error.message}`);
    error.activityLogged = true;
    throw error;
  }
}

async function capture(command, args, cwd, env = process.env) {
  try {
    log(`▶ ${command} ${args.join(' ')}`);
    const child = spawn(command, args, { cwd, env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    const { error, code, signal, stdout, stderr } = await observeProcess(child, { output: logOutput, line: completeOutput, capture: true });
    if (error) throw error;
    if (code !== 0 || signal) throw new Error(`${exitDescription(command, code, signal)}${stderr.trim() ? `: ${stderr.trim()}` : ''}`);
    return stdout.trim();
  } catch (error) {
    log(`✕ ${error.message}`);
    error.activityLogged = true;
    throw error;
  }
}

function toolEnv(base = process.env) {
  return buildTools.environment(app.getPath('userData'), base);
}

function toolOptions() {
  return { archives: Boolean(config.settings.textures || config.settings.eclipse) };
}

function toolsStatus() {
  return buildTools.status(app.getPath('userData'), process.platform, process.env, toolOptions());
}

async function checkTools(refresh = false) {
  return buildTools.check(app.getPath('userData'), { ...toolOptions(), refresh });
}

async function ensureBuildTools() {
  if (preparingTools) throw new Error('Wait for build tools to finish preparing.');
  if (active) throw new Error(`Wait for ${active.label} to finish, or stop it first.`);
  const userData = app.getPath('userData');
  const forcePrivate = process.env.SMS_FORCE_PRIVATE_TOOLS === '1';
  if (process.platform === 'darwin') {
    preparingTools = true;
    try {
      const found = await checkTools(true);
      if (!found.appleReady) throw new Error(found.message);
      if (found.ready && !forcePrivate) return;
    } finally { preparingTools = false; }
  }
  const found = toolsStatus();
  if (found.mode === 'private' || (!forcePrivate && found.ready)) return;
  preparingTools = true;
  const startedAt = Date.now();
  active = { label: 'Download build tools', child: null, detail: 'Checking download…', percent: null, startedAt };
  broadcast('activity', { label: active.label, detail: active.detail, percent: null, startedAt });
  try {
    await buildTools.prepare(userData, {
      forcePrivate,
      progress(percent, detail) {
        if (!active || active.child) return;
        if (detail && detail !== active.detail) log(detail);
        active.detail = detail || (percent === null ? 'Downloading tools…' : `Downloading tools: ${percent}%`);
        active.percent = percent;
        broadcast('activity', { label: active.label, detail: active.detail, percent, startedAt });
      },
      async run(command, args, options, label) {
        active = null;
        broadcast('activity', null);
        try { return await launch(command, args, options, label); }
        finally {
          if (preparingTools && !active) {
            active = { label: 'Download build tools', child: null, detail: 'Preparing build tools…', percent: null, startedAt };
            broadcast('activity', { label: active.label, detail: active.detail, percent: null, startedAt, canStop: false });
          }
        }
      }
    });
    log('Build tools are ready.');
  } finally {
    preparingTools = false;
    if (active && !active.child && active.startedAt === startedAt) {
      active = null;
      broadcast('activity', null);
    }
  }
}

async function updatePort() {
  requireRepo();
  port.validateRom(config.rom);
  await ensureBuildTools();
  await refreshGameSource();
  if (game.isCurrent(config.repo, config.settings, currentSource())) {
    log(config.gameSource ? 'Your game is up to date with the chosen game source.' : 'Your game is up to date for this launcher release.');
    return state();
  }
  await build();
  return state();
}

async function installPort() {
  await ensureBuildTools();
  const destination = config.repo;
  if (port.isPort(destination)) return { ok: true };
  if (fs.existsSync(destination)) {
    if (!fs.statSync(destination).isDirectory() || fs.readdirSync(destination).length)
      throw new Error('That folder is not empty. Choose another download location.');
    fs.rmdirSync(destination);
  }
  await game.checkout(destination, { run: launch, capture, env: toolEnv(), source: currentSource() });
  return { ok: true };
}

async function installEclipse(root = requireRepo()) {
  await ensureBuildTools();
  const rom = port.validateRom(config.rom);
  const cmd = port.commandFor(root, 'python', [rom], process.platform, toolEnv());
  return launch(cmd.command, cmd.args, { cwd: cmd.cwd, env: cmd.env }, 'Install Eclipse from your disc');
}

// The texture extras' pinned release, downloaded on its own (not the UHD pack
// again) when it is missing or a newer one is pinned. They only add to the UHD
// pack, so a failed download is logged and never stops setup or play.
async function updateTextureExtras(root, settings) {
  if (!port.textureExtrasWanted(root, settings)) return;
  try {
    await ensureBuildTools();
    const cmd = port.commandFor(root, 'texture-extras', [], process.platform, toolEnv());
    await launch(cmd.command, cmd.args, { cwd: cmd.cwd, env: cmd.env }, 'Install HD texture extras');
    if (!port.textureExtrasInstalled(root)) throw new Error('the installer finished without them');
  } catch (error) {
    log(`HD texture extras were not updated (${error.message}); the game plays without them and setup tries again next time.`);
  }
}

async function installTextures(root = requireRepo()) {
  await ensureBuildTools();
  if (config.settings.textures && !port.texturePackInstalled(root)) {
    const cmd = port.commandFor(root, 'textures', [], process.platform, toolEnv());
    await launch(cmd.command, cmd.args, { cwd: cmd.cwd, env: cmd.env }, 'Install UHD textures');
    if (!port.texturePackInstalled(root)) throw new Error('Texture installer finished without a usable texture pack.');
  } else if (config.settings.textures) log('HD textures are already installed.');
  await updateTextureExtras(root, config.settings);
  if (port.wantsCutscenes(config.settings) && port.cutscenePackSupported(root) && !port.cutscenePackInstalled(root)) {
    const rom = port.validateRom(config.rom);
    const cmd = port.commandFor(root, 'cutscenes', [rom], process.platform, toolEnv());
    await launch(cmd.command, cmd.args, { cwd: cmd.cwd, env: cmd.env }, 'Install HD cutscenes');
    if (!port.cutscenePackInstalled(root)) throw new Error('HD cutscene setup finished without a complete movie pack. Please retry.');
  }
  log('HD visuals are ready for the next game launch.');
  return { installed: true };
}

function binaryReady(root = config.repo, settings = config.settings) {
  return port.isPort(root) && fs.existsSync(port.binaryPath(root, settings));
}

async function build(forceFresh = false) {
  const original = requireRepo();
  const settings = { ...config.settings };
  const rom = port.validateRom(config.rom);
  const saveDirectory = currentSaveDirectory();
  makeSaveBackup('before-port-update');
  await ensureBuildTools();
  const source = currentSource();
  let root = original;
  const sourceMarker = path.join(root, 'launcher-source.json');
  const freshPinnedSource = fs.existsSync(sourceMarker) && !binaryReady(root, settings) && game.matchesSource(root, source);
  if (forceFresh || (!game.isCurrent(root, settings, source) && !freshPinnedSource)) {
    root = game.snapshotPath(config.installRoot || original, settings, source);
    if (forceFresh || (fs.existsSync(root) && !game.matchesSource(root, source))) root += `.fresh-${Date.now()}`;
    if (!fs.existsSync(root)) await game.checkout(root, { run: launch, capture, env: toolEnv(), reference: original, source });
    if (!game.matchesSource(root, source))
      throw new Error('The update folder has changed. Choose another setup location in Settings and try again.');
    game.carryUserFiles(original, root);
  }
  if (!port.hdVisualsInstalled(root, settings)) await installTextures(root);
  else await updateTextureExtras(root, settings);
  if (settings.eclipse && !fs.existsSync(path.join(root, port.ECLIPSE_ISO))) await installEclipse(root);
  const disc = port.gameDisc(root, rom, settings.eclipse);
  const env = toolEnv(port.buildEnvironment(settings, disc, root));
  env.NINJA_STATUS = '[%f/%t] ';
  const cmd = settings.eclipse
    ? port.eclipseBuildCommand(root, settings, process.platform, env)
    : port.commandFor(root, 'build', [disc], process.platform, env);
  const result = await game.buildSafely(root, settings, saveDirectory,
    () => launch(cmd.command, cmd.args, { cwd: cmd.cwd, env: cmd.env }, settings.eclipse ? 'Build Eclipse port' : 'Build Sunshine port'),
    undefined, source);
  // Preferences switch only after a successful build. Closing the app or a
  // failed download/build leaves the last installation selected and playable.
  const earlier = { ...config };
  config = { ...config, repo: root, installRoot: config.installRoot || original,
    saveDirectory, completedSetup: true,
    previousInstall: root !== original && binaryReady(original, settings)
      ? { repo: original, settings, rom, saveDirectory } : config.previousInstall };
  try { saveConfig(); }
  catch (error) { config = earlier; throw error; }
  log(`Game ${source.version} is ready. Your saved games stay in the same folder.`);
  return result;
}

async function setupGame() {
  requireRepo();
  port.validateRom(config.rom);
  if (!game.isCurrent(config.repo, config.settings, currentSource())) await build();
  else if (!port.hdVisualsInstalled(config.repo, config.settings)) await installTextures();
  else await updateTextureExtras(config.repo, config.settings);
  return state();
}

const PLAYER_SETTINGS = [
  'volume', 'invertCameraX', 'invertCameraY', 'keyBindings', 'padBindings', 'softTrigger', 'freeCamera', 'cameraSpeed', 'mouseCamera',
  'mouseSensitivity', 'skipMovies', 'heatHaze', 'buttonPrompts', 'promptPad', 'hdr', 'hdrCalibration', 'hdrPaperWhite', 'hdrPeak', 'hdrContrast', 'hdrSaturation', 'hdrHighlights', 'overlay', 'fullscreenMode', 'exclusiveResolution', 'display', 'vsync',
  'msaa', 'fxaa', 'anisotropic', 'sharpen', 'fsrMode', 'brightness', 'aspect', 'presentFilter'
];
function promptDirectory() { return path.join(app.getPath('userData'), 'prompts'); }

function playerSettings(settings) { return Object.fromEntries(PLAYER_SETTINGS.map(key => [key, settings[key]])); }

// What Windows reports about each display's HDR: the game's --display-info (its peak brightness as calibrated,
// SDR content brightness and HDR calibration profile), for Settings → HDR.
async function displayInfo() {
  if (process.platform !== 'win32') return { displays: [], error: 'HDR settings are for Windows.' };
  const root = config.repo, settings = config.settings;
  if (!root || !binaryReady(root, settings)) return { displays: [], error: 'Set up the game first to read your display.' };
  const recordedTools = game.installed(root, settings)?.toolRoot || game.compilerToolRoot(root, settings);
  const base = { ...process.env, SMS_ARCH: settings.arch };
  const env = recordedTools ? buildTools.environmentAtRoot(recordedTools, base) : toolEnv(base);
  const cmd = port.commandFor(root, 'run', ['--display-info'], process.platform, env);
  const stdout = await new Promise(resolve => execFile(cmd.command, cmd.args,
    { cwd: cmd.cwd, env: cmd.env, timeout: 15000, windowsHide: true }, (_error, out) => resolve(String(out || ''))));
  const line = stdout.split(/\r?\n/).find(text => text.startsWith('{"displays"'));
  if (!line) return { displays: [], error: 'This game version cannot read your display yet. Update the game to see it.' };
  try { return { displays: JSON.parse(line).displays || [] }; }
  catch (_) { return { displays: [], error: 'The game reported your display in a way the launcher did not understand.' }; }
}

async function play(installation = null) {
  const root = installation?.repo || requireRepo();
  // Controls, sound, display and picture choices are the player's, not the build's: an earlier install plays with them too.
  const settings = installation ? { ...installation.settings, ...playerSettings(config.settings) } : config.settings;
  const rom = port.validateRom(installation?.rom || config.rom);
  if (!binaryReady(root, settings)) throw new Error('Set up this version of the game before playing.');
  if (!port.hdVisualsInstalled(root, settings))
    throw new Error('HD visuals need to be set up. Return to the main screen to finish setup, or turn them off in Settings.');
  const disc = port.gameDisc(root, rom, settings.eclipse);
  makeSaveBackup('before-play');
  const saveDir = saves.prepareSaveDirectory(currentSaveDirectory());
  const recordedTools = game.installed(root, settings)?.toolRoot || game.compilerToolRoot(root, settings);
  const env = recordedTools
    ? buildTools.environmentAtRoot(recordedTools, { ...port.buildEnvironment(settings, disc, root), SMS_SAVE_DIR: saveDir })
    : toolEnv({ ...port.buildEnvironment(settings, disc, root), SMS_SAVE_DIR: saveDir });
  // Changed keys go to a file of the launcher's own, so a bindings.txt kept by hand in the game folder is left alone.
  if (Object.keys(settings.keyBindings || {}).length || Object.keys(settings.padBindings || {}).length) {
    const file = path.join(app.getPath('userData'), 'bindings.txt');
    fs.writeFileSync(file, bindings.fileText(settings.keyBindings, settings.padBindings));
    env.SMS_BINDINGS = file;
  }
  // The prompt images the Controls page drew from these bindings (save-prompts).
  if (settings.buttonPrompts && settings.buttonPrompts !== 'gamecube') env.SMS_BUTTON_PROMPT_DIR = promptDirectory();
  if (config.settings.discordPresence) env.SMS_PRESENCE = '1';
  if (portableRoot && !env.SMS_GX_SHADER_CACHE) {
    // The game only makes the folder for its default location.
    env.SMS_GX_SHADER_CACHE = portable.shaderCache(portableRoot, settings.arch);
    fs.mkdirSync(path.dirname(env.SMS_GX_SHADER_CACHE), { recursive: true });
  }
  if (portableRoot && !env.SMS_TEXTURE_PACK_CACHE) env.SMS_TEXTURE_PACK_CACHE = portable.textureCache(portableRoot);
  const cmd = settings.eclipse
    ? port.eclipseRunCommand(root, settings, disc, process.platform, env)
    : port.commandFor(root, 'run', [disc], process.platform, env);
  const startedAt = Date.now();
  startPresence(startedAt, settings.eclipse);
  try {
    return await launch(cmd.command, cmd.args, { cwd: cmd.cwd, env: cmd.env }, 'Play Super Mario Sunshine', true);
  } finally {
    stopPresence(startedAt);
    try { makeSaveBackup('after-play'); }
    catch (error) { log(`Save backup failed: ${error.message}`); }
  }
}

// Time spent in the game, in milliseconds, for Discord; kept apart from preferences.
function playtimeFile() { return path.join(app.getPath('userData'), 'playtime.json'); }

function playtime() {
  try {
    const { ms } = JSON.parse(fs.readFileSync(playtimeFile(), 'utf8'));
    return Number.isFinite(ms) && ms > 0 ? ms : 0;
  } catch (_) { return 0; }
}

// Discord shows the game only while it runs, and only with the setting on.
function startPresence(startedAt, eclipse) {
  if (!config.settings.discordPresence) return;
  discordPresence ||= discord.createPresence({ log });
  const before = playtime();
  const show = game => discordPresence.set(presence.activity(game,
    { startedAt, eclipse, playtime: before + Date.now() - startedAt }));
  onPresence = line => { const game = presence.parseLine(line); if (game) show(game); };
  show(null);
}

function stopPresence(startedAt) {
  onPresence = null;
  discordPresence?.stop();
  try { fs.writeFileSync(playtimeFile(), JSON.stringify({ ms: playtime() + Date.now() - startedAt })); }
  catch (error) { log(`Could not save play time: ${error.message}`); }
}

async function launchGame() {
  requireRepo();
  port.validateRom(config.rom);
  if (!binaryReady() || (config.settings.autoUpdate && !game.isCurrent(config.repo, config.settings, currentSource()))) await build();
  if (!port.hdVisualsInstalled(config.repo, config.settings)) await installTextures();
  else await updateTextureExtras(config.repo, config.settings);
  return play();
}

// Why `--play` opens the launcher instead of the game, or null if the game can start as it is.
function directPlayBlocker() {
  if (!port.isPort(config.repo) || !binaryReady()) return 'the game is not set up yet.';
  try { port.validateRom(config.rom); } catch (error) { return error.message; }
  if (config.settings.autoUpdate && !game.isCurrent(config.repo, config.settings, currentSource()))
    return 'a game update is ready.';
  if (!port.hdVisualsInstalled(config.repo, config.settings)) return 'HD visuals need to be set up.';
  return null;
}

// Plays with the launcher's settings, without downloading or building anything.
// False when the launcher should open: the game needs setup or an update, or it
// failed (the activity log shows why).
async function playDirectly() {
  const blocker = directPlayBlocker();
  if (blocker) {
    log(`Opened the launcher instead of the game: ${blocker}`);
    return false;
  }
  try { await exclusive('Play Super Mario Sunshine', () => play(), true); }
  catch (_) { return false; }
  return true;
}

async function playInstalled() {
  const root = requireRepo();
  // Playing without setup uses only packs already on disk. Keep the player's
  // preferences so choosing setup later still installs the requested visuals.
  const settings = { ...config.settings,
    textures: config.settings.textures && port.texturePackInstalled(root),
    cutscenes: config.settings.cutscenes && port.cutscenePackInstalled(root) };
  if (settings.textures !== config.settings.textures || settings.cutscenes !== config.settings.cutscenes)
    log('Playing the installed game without missing HD downloads. Your visual settings are saved for later setup.');
  return play({ repo: root, settings });
}

async function clean(dryRun) {
  await ensureBuildTools();
  const root = requireRepo();
  if (!dryRun && saves.cleanupWouldRemoveSaves(root, currentSaveDirectory()))
    throw new Error('Your memory card is inside a build folder. Move it outside the build folders before cleanup.');
  if (!dryRun) makeSaveBackup('before-cleanup');
  const env = toolEnv();
  delete env.SMS_ARCH;
  const cmd = port.commandFor(root, 'clean', dryRun ? ['--dry-run'] : [], process.platform, env);
  return launch(cmd.command, cmd.args, { cwd: cmd.cwd, env: cmd.env }, dryRun ? 'Preview cleanup' : 'Clean build output');
}

function state() {
  const info = port.platformInfo();
  let romError = '';
  if (config.rom) { try { port.validateRom(config.rom); } catch (error) { romError = error.message; } }
  const repoReady = port.isPort(config.repo);
  const ready = repoReady && binaryReady();
  const installedBuild = ready ? game.installed(config.repo, config.settings) : null;
  return {
    config, platform: info, repoReady, portable: Boolean(portableRoot),
    romReady: Boolean(config.rom) && !romError, romError,
    eclipseInstalled: repoReady && fs.existsSync(path.join(config.repo, port.ECLIPSE_ISO)),
    hdVisualsReady: repoReady && port.hdVisualsInstalled(config.repo, config.settings),
    texturesInstalled: repoReady && port.texturePackInstalled(config.repo),
    cutscenesInstalled: repoReady && port.cutscenePackInstalled(config.repo),
    cutscenesSupported: repoReady && port.cutscenePackSupported(config.repo),
    cutsceneRequirements: repoReady ? port.cutscenePackRequirements(config.repo) : null,
    binaryReady: ready,
    tools: toolsStatus(),
    gameSource: config.gameSource ? { override: config.gameSource.override, version: config.gameSource.resolved.version } : null,
    game: { launcherVersion: app.getVersion(), availableVersion: currentSource().version,
      installedVersion: installedBuild?.gameVersion || null,
      installedToolVersion: installedBuild?.toolVersion || null,
      toolVersion: buildTools.toolsetFor(), needsUpdate: repoReady && !game.isCurrent(config.repo, config.settings, currentSource()),
      previousReady: Boolean(config.previousInstall && binaryReady(config.previousInstall.repo, config.previousInstall.settings)) },
    active: activityState(), appUpdate, online, logs: activityLog.snapshot(), logResetSequence: activityLog.resetSequence(),
    saveDirectory: currentSaveDirectory(), backupDirectory: saves.backupRoot(),
    backups: saves.listBackups().filter(item => item.source === currentSaveDirectory())
  };
}

// Stable never sees a pre-release. Beta and pull request previews are opt-in,
// and may be older than what is installed, so leaving one can go back down.
function applyUpdateChannel() {
  if (!updater) return;
  const channel = config.settings.updateChannel;
  const url = process.env.SMS_LAUNCHER_UPDATE_URL;
  updater.setFeedURL(url ? { provider: 'generic', url } : updateChannel.feedFor(channel));
  updater.channel = 'latest';
  updater.allowPrerelease = channel !== 'stable';
  updater.allowDowngrade = channel !== 'stable' || app.getVersion().includes('-');
}

async function checkAppUpdate() {
  if (!updater) return appUpdate;
  try { await updater.checkForUpdates(); }
  catch (error) {
    const missing = config.settings.updateChannel !== 'stable' && /\b404\b/.test(error.message);
    appUpdate = { state: 'error', message: missing
      ? `${updateChannel.channelLabel(config.settings.updateChannel)} is no longer available. Choose another update channel in Settings.`
      : `Launcher update check failed: ${error.message}` };
    broadcast('app-update', appUpdate);
    log(appUpdate.message);
  }
  return appUpdate;
}

async function updateChannels() {
  let releases = [];
  try {
    const response = await net.fetch(`https://api.github.com/repos/${updateChannel.REPOSITORY}/releases?per_page=100`,
      { headers: { Accept: 'application/vnd.github+json' } });
    if (response.ok) releases = await response.json();
  } catch (_) { /* offline: Stable and the current choice are still offered */ }
  const channels = updateChannel.channelsFromReleases(releases);
  const chosen = config.settings.updateChannel;
  if (!channels.some(item => item.id === chosen))
    channels.push({ id: chosen, label: updateChannel.channelLabel(chosen), detail: 'No longer available', missing: true });
  return channels;
}

let releaseNotes = null;
// The published changelog, or the copy bundled with this launcher when GitHub
// cannot be reached. Development runs read the local file so edits show.
async function loadChangelog() {
  if (releaseNotes) return releaseNotes;
  let bundled = changelog.normalize(null);
  try { bundled = changelog.normalize(JSON.parse(fs.readFileSync(path.join(__dirname, '..', changelog.FILE), 'utf8'))); }
  catch (error) { log(`Could not read the bundled changelog: ${error.message}`); }
  if (!app.isPackaged && !process.env.SMS_CHANGELOG_URL) return (releaseNotes = bundled);
  try {
    const response = await net.fetch(changelog.url(), { cache: 'no-cache', signal: AbortSignal.timeout(8000) });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    releaseNotes = changelog.merge(changelog.normalize(await response.json()), bundled);
    return releaseNotes;
  } catch (error) {
    log(`Showing the changelog bundled with this launcher: ${error.message}`);
    return bundled;
  }
}

let betaShown = null;
async function changelogState() {
  const { releases, beta } = await loadChangelog();
  const options = { current: app.getVersion(), returning: config.completedSetup };
  betaShown = changelog.isBeta(app.getVersion()) ? beta : null;
  return { releases, current: changelog.baseVersion(app.getVersion()),
    unseen: changelog.unseen(releases, { ...options, seen: config.changelogSeen }).map(release => release.version),
    beta: betaShown && { ...betaShown, unseen: changelog.unseenBeta(betaShown, { ...options, seen: config.changelogBetaSeen }) } };
}

// The release only moves forward, so leaving a preview for an older release
// and coming back does not repeat notes already shown.
function markChangelogSeen() {
  const version = changelog.baseVersion(app.getVersion());
  const betaKey = changelog.betaKey(betaShown);
  const newer = version && (!config.changelogSeen || changelog.compareVersions(version, config.changelogSeen) > 0);
  if (!newer && (!betaKey || betaKey === config.changelogBetaSeen)) return;
  if (newer) config.changelogSeen = version;
  if (betaKey) config.changelogBetaSeen = betaKey;
  saveConfig();
}

function heartbeatPayload() {
  const installed = port.isPort(config.repo) && binaryReady() ? game.installed(config.repo, config.settings) : null;
  return telemetry.payload({ installId: config.installId, launcherVersion: app.getVersion(),
    gameVersion: installed?.gameVersion, availableVersion: currentSource().version,
    channel: config.settings.updateChannel, playing: active?.label === 'Play Super Mario Sunshine' });
}

// Development runs stay out of the numbers unless pointed at a test API.
function setupTelemetry() {
  const url = telemetry.endpoint();
  if (!url || (!app.isPackaged && !process.env.SMS_TELEMETRY_URL)) return;
  reporter = telemetry.createReporter({
    url, fetch: (target, options) => net.fetch(target, options),
    sharing: () => config.settings.shareUsage, getPayload: heartbeatPayload,
    onCount: count => { online = count; broadcast('online', online); }
  });
  reporter.start();
}

function setupAppUpdater() {
  const url = process.env.SMS_LAUNCHER_UPDATE_URL;
  const bundledFeed = fs.existsSync(path.join(process.resourcesPath, 'app-update.yml'));
  // The updater would replace a portable launcher with the installer build.
  if (portableRoot) {
    appUpdate = { state: 'unconfigured', message: 'Portable launcher: download new versions from the releases page.' };
    ipcMain.handle('check-app-update', () => appUpdate);
    return;
  }
  if (!app.isPackaged || (!url && !bundledFeed) || (process.platform === 'linux' && !process.env.APPIMAGE)) {
    appUpdate = { state: 'unconfigured', message: app.isPackaged ? 'Launcher updates are unavailable for this install.' : 'Launcher updates come with new releases.' };
    ipcMain.handle('check-app-update', () => appUpdate);
    return;
  }
  ({ autoUpdater: updater } = require('electron-updater'));
  // Updates keep the AppImage's name and folder, so Steam shortcuts to it keep working
  if (process.platform === 'linux') appImageUpdate.keepAppImagePath(updater);
  updater.autoDownload = true;
  updater.autoInstallOnAppQuit = true;
  applyUpdateChannel();
  const set = (state, message) => {
    appUpdate = { state, message }; broadcast('app-update', appUpdate);
    if (state !== 'downloading' || !message.startsWith('Downloading launcher update:')) log(message);
  };
  updater.on('checking-for-update', () => set('checking', 'Checking launcher updates…'));
  updater.on('update-available', info => set('downloading', `Downloading launcher ${info.version}…`));
  updater.on('update-not-available', () => set('current', 'Launcher is up to date.'));
  updater.on('download-progress', progress => set('downloading', `Downloading launcher update: ${Math.round(progress.percent)}%`));
  updater.on('update-downloaded', info => set('ready', `Launcher ${info.version} downloaded; it will install when you quit.`));
  updater.on('error', error => set('error', `Launcher update check failed: ${error.message}`));
  const check = () => { if (config.settings.autoUpdate) checkAppUpdate(); };
  setTimeout(check, 10000);
  setInterval(check, 30 * 60 * 1000);
  ipcMain.handle('check-app-update', () => checkAppUpdate());
  ipcMain.handle('install-app-update', () => {
    if (appUpdate.state !== 'ready') throw new Error('No launcher update is ready to install.');
    if (operation || active) throw new Error('Finish the current task or close the game before installing the update.');
    // Install silently, then relaunch from the installer itself. Opening the
    // launcher by hand while the installer still runs gets that copy closed.
    updater.quitAndInstall(true, true);
  });
}

function registerHandlers() {
  ipcMain.handle('copy-activity-log', () => {
    clipboard.writeText(sessionLog.read([...partialOutput.values()].sort((a, b) => a.id - b.id)));
    return { copied: true };
  });
  ipcMain.handle('save-activity-log', async () => {
    const chosen = await dialog.showSaveDialog(window, { title: 'Save activity log',
      defaultPath: path.join(app.getPath('downloads'), 'SMS-activity.log'),
      filters: [{ name: 'Activity log', extensions: ['log'] }] });
    if (chosen.canceled || !chosen.filePath) return { cancelled: true };
    sessionLog.save(chosen.filePath, [...partialOutput.values()].sort((a, b) => a.id - b.id));
    return { saved: true };
  });
  const senderWindow = event => BrowserWindow.fromWebContents(event.sender);
  ipcMain.handle('window-state', event => {
    const target = senderWindow(event);
    return { fullscreen: target?.isFullScreen() || false, minimized: target?.isMinimized() || false };
  });
  ipcMain.handle('window-minimize', event => {
    const target = senderWindow(event);
    target?.minimize();
    return { minimized: target?.isMinimized() || false };
  });
  ipcMain.handle('window-toggle-full-screen', event => {
    const target = senderWindow(event);
    if (!target) return { fullscreen: false };
    target.setFullScreen(!target.isFullScreen());
    return { fullscreen: target.isFullScreen() };
  });
  ipcMain.handle('window-close', event => senderWindow(event)?.close());
  ipcMain.handle('state', async () => { await checkTools(); return state(); });
  ipcMain.handle('check-tools', async () => { await checkTools(true); return state(); });
  ipcMain.handle('copy-mac-command', (_event, index) => {
    if (process.platform !== 'darwin') throw new Error('This help is for macOS.');
    const commands = toolsStatus().commands.split('\n').filter(Boolean);
    if (!Number.isInteger(index) || !commands[index]) throw new Error('Check your Mac tools again.');
    clipboard.writeText(commands[index]);
  });
  // The button prompt strips the renderer drew (src/prompts.js), one PNG per style.
  ipcMain.handle('save-prompts', (_event, images) => {
    const dir = promptDirectory();
    fs.mkdirSync(dir, { recursive: true });
    for (const style of prompts.DRAWN) {
      const match = /^data:image\/png;base64,([A-Za-z0-9+/=]+)$/.exec(images?.[style] || '');
      if (match) fs.writeFileSync(path.join(dir, `${style}.png`), Buffer.from(match[1], 'base64'));
    }
  });
  ipcMain.handle('save-settings', (_event, input) => {
    if (operation || active) throw new Error('Finish the current task before changing settings.');
    const settings = port.normalizeSettings(input);
    if (settings.arch !== config.settings.arch || settings.eclipse !== config.settings.eclipse)
      config.previousInstall = port.playableInstall({ ...config, saveDirectory: currentSaveDirectory() }) || config.previousInstall;
    const channelChanged = settings.updateChannel !== config.settings.updateChannel;
    const sharingChanged = settings.shareUsage !== config.settings.shareUsage;
    config.settings = settings;
    saveConfig();
    if (sharingChanged) reporter?.tick();
    if (channelChanged) {
      applyUpdateChannel();
      log(`Launcher updates now follow ${updateChannel.channelLabel(settings.updateChannel)}.`);
      checkAppUpdate();
    }
    return state();
  });
  ipcMain.handle('update-channels', () => updateChannels());
  ipcMain.handle('choose-rom', async () => {
    if (operation || active) throw new Error('Finish the current task before changing game files.');
    const chosen = await dialog.showOpenDialog(window, {
      title: 'Choose a file from your own Super Mario Sunshine disc',
      properties: ['openFile'], filters: [{ name: 'Game disc files', extensions: ['iso', 'gcm', 'ciso'] }]
    });
    if (chosen.canceled) return state();
    config.rom = port.validateRom(chosen.filePaths[0]);
    saveConfig();
    return state();
  });
  ipcMain.handle('choose-repo', async () => {
    if (portableRoot) throw new Error('The portable launcher keeps its setup files in its own folder.');
    if (operation || active) throw new Error('Finish the current task before changing game files.');
    const chosen = await dialog.showOpenDialog(window, { title: 'Choose a folder with setup files', properties: ['openDirectory'] });
    if (chosen.canceled) return state();
    if (!port.isPort(chosen.filePaths[0])) throw new Error('That folder does not have the setup files this launcher needs.');
    config.repo = chosen.filePaths[0];
    config.installRoot = null;
    config.saveDirectory = null;
    config.previousInstall = null;
    config.completedSetup = fs.existsSync(port.binaryPath(config.repo, config.settings));
    saveConfig();
    return state();
  });
  ipcMain.handle('choose-location', async () => {
    if (portableRoot) throw new Error('The portable launcher keeps its setup files in its own folder.');
    if (operation || active) throw new Error('Finish the current task before changing game files.');
    const chosen = await dialog.showOpenDialog(window, { title: 'Choose where to download setup files', properties: ['openDirectory'] });
    if (chosen.canceled) return state();
    const destination = path.join(chosen.filePaths[0], 'sms-pc-port');
    if (fs.existsSync(destination) && (!fs.statSync(destination).isDirectory() || fs.readdirSync(destination).length))
      throw new Error('That location already has a game setup folder. Choose another location.');
    config.repo = destination;
    config.installRoot = null;
    config.saveDirectory = null;
    config.previousInstall = null;
    config.completedSetup = fs.existsSync(port.binaryPath(config.repo, config.settings));
    saveConfig();
    return state();
  });
  const actions = { 'install-port': ['Download port source', installPort],
    'update-port': ['Update game', updatePort], 'install-eclipse': ['Install Eclipse', () => installEclipse()],
    'install-textures': ['Install UHD textures', () => installTextures()], 'build': ['Build game', () => build(true)],
    'setup-game': ['Build game', setupGame], 'play': ['Play Super Mario Sunshine', playInstalled],
    'launch-game': ['Prepare game', launchGame],
    'play-previous': ['Play Super Mario Sunshine', () => {
      if (!config.previousInstall) throw new Error('There is no previous version available.');
      return play(config.previousInstall);
    }] };
  for (const [channel, [label, callback]] of Object.entries(actions))
    ipcMain.handle(channel, () => exclusive(label, callback, ['play', 'launch-game', 'play-previous'].includes(channel)));
  ipcMain.handle('set-game-source', (_event, input) => exclusive('Choose game source', () => chooseGameSource(input)));
  ipcMain.handle('clean-preview', () => exclusive('Preview cleanup', () => clean(true)));
  ipcMain.handle('backup-saves', () => exclusive('Back up saves', () => makeSaveBackup('manual')));
  ipcMain.handle('steam-status', () => steamStatus());
  ipcMain.handle('add-to-steam', (_event, options) => exclusive('Add to Steam', () => addToSteam(options)));
  ipcMain.handle('import-dolphin-save', (_event, file) => exclusive('Import Dolphin save', async () => {
    if (file == null) {
      const chosen = await dialog.showOpenDialog(window, { title: 'Import Dolphin save',
        properties: ['openFile'], filters: [{ name: 'Dolphin save', extensions: ['gci'] }] });
      if (chosen.canceled || !chosen.filePaths.length) return { cancelled: true };
      file = chosen.filePaths[0];
    }
    const converted = gciSave.readGci(file);
    const directory = currentSaveDirectory();
    const existing = fs.existsSync(path.join(directory, `${converted.name}.dat`));
    const warning = converted.badBlocks.length
      ? `\n\nSave checksums failed in block(s) ${converted.badBlocks.join(', ')}. Some blocks may be unused or damaged; the game may be unable to load this save.` : '';
    const answer = await dialog.showMessageBox(window, {
      type: existing || warning ? 'warning' : 'question',
      buttons: ['Cancel', 'Import save'], defaultId: 0, cancelId: 0,
      message: existing ? 'Replace your Sunshine save with this Dolphin save?' : 'Import this Dolphin save?',
      detail: `${path.basename(file)}\n\nDestination: ${directory}\n\nThis imports the whole Sunshine save, including all three save slots. Your current memory card will be backed up first. The original .gci file is kept.${warning}`
    });
    if (answer.response !== 1) return { cancelled: true };
    const result = gciSave.importGci(converted, directory);
    log(`Imported Dolphin save into ${directory}.${result.previous ? ` Previous saves: ${result.previous}` : ''}`);
    return result;
  }));
  ipcMain.handle('restore-saves', async (_event, id) => {
    if (operation || active) throw new Error('Stop the running game or task before restoring saves.');
    if (!saves.listBackups().some(item => item.id === id && item.source === currentSaveDirectory()))
      throw new Error('Choose a backup for this memory card.');
    const answer = await dialog.showMessageBox(window, {
      type: 'warning', buttons: ['Cancel', 'Restore memory card'], defaultId: 0, cancelId: 0,
      message: 'Restore this backup?',
      detail: "We'll back up your current saves first, then replace matching files."
    });
    if (answer.response !== 1) return { cancelled: true };
    const result = saves.restoreBackup(id, currentSaveDirectory());
    log(`Restored ${result.restored} memory card file(s) from ${id}.`);
    return result;
  });
  ipcMain.handle('open-backups', () => {
    fs.mkdirSync(saves.backupRoot(), { recursive: true, mode: 0o700 });
    return shell.openPath(saves.backupRoot());
  });
  ipcMain.handle('clean', () => exclusive('Clean build output', async () => {
    const answer = await dialog.showMessageBox(window, {
      type: 'warning', buttons: ['Cancel', 'Free up space'], defaultId: 0, cancelId: 0,
      message: 'Remove files the launcher can make again?', detail: 'The next play may take longer. Your disc file and saved games will be kept.'
    });
    return answer.response === 1 ? clean(false) : { cancelled: true };
  }));
  ipcMain.handle('stop', () => {
    if (!active) return false;
    if (!active.child) return false;
    if (process.platform === 'win32') spawn('taskkill', ['/PID', String(active.child.pid), '/T', '/F'], { windowsHide: true });
    else { try { process.kill(-active.child.pid, 'SIGTERM'); } catch (error) { if (error.code !== 'ESRCH') throw error; } }
    return true;
  });
  ipcMain.handle('open-docs', () => shell.openPath(path.join(requireRepo(), 'BUILD.md')));
  // Only this fixed invite opens: the page never chooses a URL.
  ipcMain.handle('open-discord', () => shell.openExternal(DISCORD_INVITE));
  ipcMain.handle('changelog', () => changelogState());
  ipcMain.handle('changelog-seen', () => markChangelogSeen());
  ipcMain.handle('open-changelog-page', () => shell.openExternal(changelog.PAGE_URL));
  ipcMain.handle('open-hdr-calibration', () => shell.openExternal(HDR_CALIBRATION_APP));
  ipcMain.handle('display-info', () => displayInfo());
}

const WINDOW_SIZE = { width: 1080, height: 760, minWidth: 900, minHeight: 600 };

function createWindow() {
  // Only Windows display scaling is capped: a Mac's Retina 2x is its normal size.
  const windowsScaling = process.platform === 'win32';
  const zoom = windowsScaling ? displayScale.displayZoom(screen.getPrimaryDisplay()) : 1;
  window = new BrowserWindow({
    ...displayScale.scaledSize(WINDOW_SIZE, zoom),
    frame: false, backgroundColor: '#0a3045', title: 'SMS Launcher',
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: true }
  });
  if (windowsScaling) displayScale.followDisplayScale(window, screen, WINDOW_SIZE, zoom);
  window.setMenuBarVisibility(false);
  const sendWindowState = () => broadcast('window-state', { fullscreen: window.isFullScreen() });
  window.on('enter-full-screen', sendWindowState);
  window.on('leave-full-screen', sendWindowState);
  window.loadFile(path.join(__dirname, 'index.html'));
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('console-message', details => {
    if (details.level === 'error' || details.level === 'warning')
      log(`Renderer ${details.level}: ${details.message} (${details.sourceId}:${details.lineNumber})`);
  });
}

if (ownsInstance) app.whenReady().then(async () => {
  loadConfig();
  registerHandlers();
  setupTelemetry();
  if (playOnStart && await playDirectly()) return app.quit();
  createWindow();
  // Only with the window: a launcher update installs on quit, which `--play` does as the game closes.
  setupAppUpdater();
  // The bundled manifest is the update check. Source downloads and builds
  // start with Update & play, so opening the launcher never changes a game.
});

app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });

app.on('before-quit', () => {
  flushPartialOutput();
  // Stop an in-flight compiler before another launcher process can recover it.
  if (!active?.child || active.label === 'Play Super Mario Sunshine') return;
  if (process.platform === 'win32') spawn('taskkill', ['/PID', String(active.child.pid), '/T', '/F'], { windowsHide: true });
  else { try { process.kill(-active.child.pid, 'SIGTERM'); } catch (_) { /* already exited */ } }
});
