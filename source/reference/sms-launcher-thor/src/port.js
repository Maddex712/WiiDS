'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { normalizeChannel } = require('./update-channel');
const bindings = require('./bindings');
const prompts = require('./prompts');
const presets = require('./presets');
const { prependWindowsPath } = require('./windows-path');

const PORT_URL = 'https://github.com/chasem-dev/sms-pc-port.git';
const ECLIPSE_ISO = path.join('mods', 'eclipse', 'Super Mario Eclipse v1.1.0.iso');

function platformInfo(platform = process.platform) {
  if (platform === 'linux') return { name: 'Linux', id: 'linux', arches: ['64', '32'], defaultArch: '64' };
  if (platform === 'darwin') return { name: 'macOS', id: 'macos', arches: ['64'], defaultArch: '64' };
  if (platform === 'win32') return { name: 'Windows', id: 'windows', arches: ['64', '32'], defaultArch: '64' };
  throw new Error(`This port does not support ${platform}.`);
}

function isPort(root) {
  return Boolean(root && ['CMakeLists.txt', 'build.sh', 'run.sh', 'clean.sh', path.join('tools', 'mods', 'get.py')]
    .every(file => fs.existsSync(path.join(root, file))));
}

function validateRom(file) {
  if (!file || typeof file !== 'string') throw new Error('Choose your own game disc file first.');
  const absolute = path.resolve(file);
  if (!['.iso', '.gcm', '.ciso'].includes(path.extname(absolute).toLowerCase()))
    throw new Error('Choose an ISO, GCM, or CISO game disc file.');
  let stat;
  try { stat = fs.statSync(absolute); }
  catch (error) {
    if (error.code === 'ENOENT') throw new Error('That game file cannot be found. Choose it again.');
    throw error;
  }
  if (!stat.isFile()) throw new Error('Choose a game disc file, not a folder.');
  const fd = fs.openSync(absolute, 'r');
  try {
    const first = Buffer.alloc(0x20);
    if (fs.readSync(fd, first, 0, first.length, 0) !== first.length) throw new Error('This file is too small to be a game disc copy.');
    let offset = 0;
    if (first.toString('ascii', 0, 4) === 'CISO') {
      const blockSize = first.readUInt32LE(4);
      if (blockSize < 0x20 || blockSize > 0x200000 || first[8] !== 1)
        throw new Error('This compressed game disc file appears to be damaged.');
      offset = 0x8000;
      if (fs.readSync(fd, first, 0, first.length, offset) !== first.length) throw new Error('This compressed game disc file appears to be incomplete.');
    }
    if (first.toString('ascii', 0, 6) !== 'GMSE01' || first[7] !== 0 || first.readUInt32BE(0x1c) !== 0xc2339f3d)
      throw new Error('Use a copy of your own North American Super Mario Sunshine disc (GMSE01, original revision).');
    return absolute;
  } finally { fs.closeSync(fd); }
}

// A whole-number setting within lo..hi, or the fallback when unset or unreadable.
function numberSetting(value, fallback, lo, hi) {
  const number = value === null || value === '' || value === undefined ? NaN : Number(value);
  return Number.isFinite(number) ? Math.min(hi, Math.max(lo, Math.round(number))) : fallback;
}
function choiceSetting(value, choices, fallback) { return choices.includes(value) ? value : fallback; }

function normalizeSettings(input = {}, platform = process.platform) {
  const info = platformInfo(platform);
  const arch = info.arches.includes(String(input.arch)) ? String(input.arch) : info.defaultArch;
  const widescreen = ['off', '16:9', '16:10', '21:9'].includes(input.widescreen) ? input.widescreen : '16:9';
  const resolution = [1, 2, 3, 4].includes(Number(input.resolution)) ? Number(input.resolution) : 4;
  // Migrate the previous 60 fps toggle without changing a saved preference.
  const frameRate = [30, 60, 120].includes(Number(input.frameRate))
    ? Number(input.frameRate) : input.fps60 === false ? 30 : 60;
  const volume = Number.isFinite(Number(input.volume)) && input.volume !== null && input.volume !== ''
    ? Math.min(100, Math.max(0, Math.round(Number(input.volume)))) : 100;
  const settings = {
    arch, widescreen, resolution,
    frameRate,
    hudEdges: input.hudEdges !== false,
    fullscreen: Boolean(input.fullscreen),
    // Borderless (desktop) fullscreen, or exclusive, which switches the display to exclusiveResolution.
    fullscreenMode: choiceSetting(input.fullscreenMode, ['desktop', 'exclusive'], 'desktop'),
    exclusiveResolution: /^\d{3,5}x\d{3,5}$/.test(String(input.exclusiveResolution)) ? String(input.exclusiveResolution) : 'desktop',
    // The monitor under the mouse (usually the launcher's), or the primary one.
    display: choiceSetting(input.display, ['launcher', 'primary'], 'launcher'),
    vsync: choiceSetting(input.vsync, ['off', 'on', 'adaptive'], 'off'),
    // Straight to the title screen, and the game's frame-time overlay at start.
    skipMovies: Boolean(input.skipMovies),
    // The heat-wave shimmer in sunny areas; on unless turned off.
    heatHaze: input.heatHaze !== false,
    // Which buttons the game's text shows (src/prompts.js): the GameCube's own, or another controller's or the keys.
    buttonPrompts: prompts.normalizeStyle(input.buttonPrompts),
    promptPad: prompts.normalizePad(input.promptPad),
    // HDR output (Windows): off unless chosen. With hdrCalibration the game takes peak brightness from the
    // Windows HDR Calibration profile and paper white from Windows' SDR content brightness; otherwise these
    // nits. Contrast and saturation are percent (100 unchanged); highlights is how far whites reach toward the peak.
    hdr: Boolean(input.hdr),
    hdrCalibration: input.hdrCalibration !== false,
    hdrPaperWhite: numberSetting(input.hdrPaperWhite, 200, 80, 500),
    hdrPeak: numberSetting(input.hdrPeak, 1000, 400, 4000),
    hdrContrast: numberSetting(input.hdrContrast, 100, 50, 150),
    hdrSaturation: numberSetting(input.hdrSaturation, 100, 0, 200),
    hdrHighlights: numberSetting(input.hdrHighlights, 40, 0, 100),
    overlay: Boolean(input.overlay),
    invertCameraX: input.invertCameraX !== false,
    invertCameraY: input.invertCameraY !== false,
    // The camera stays where it is put instead of swinging back behind Mario; manual turning speed; mouse look.
    freeCamera: Boolean(input.freeCamera),
    cameraSpeed: numberSetting(input.cameraSpeed, 100, 10, 400),
    mouseCamera: Boolean(input.mouseCamera),
    mouseSensitivity: numberSetting(input.mouseSensitivity, 100, 5, 1000),
    // Picture: anti-aliasing, texture filtering, sharpening, brightness (percent; 100 unchanged), fit and scaler.
    msaa: [0, 2, 4, 8].includes(Number(input.msaa)) ? Number(input.msaa) : 0,
    fxaa: Boolean(input.fxaa),
    anisotropic: [0, 2, 4, 8, 16].includes(Number(input.anisotropic)) ? Number(input.anisotropic) : 0,
    sharpen: numberSetting(input.sharpen, 0, 0, 100),
    brightness: numberSetting(input.brightness, 100, 50, 200),
    aspect: choiceSetting(input.aspect, ['keep', 'stretch', 'integer'], 'keep'),
    // fsr: AMD FSR 1, which upscales a low picture sharpness with clean edges (the game's sharpening sets its strength).
    presentFilter: choiceSetting(input.presentFilter, ['bilinear', 'sharp', 'nearest', 'fsr', 'nis'], 'bilinear'),
    // With FSR 1: how far below the picture's size on screen the game renders (it sets the internal resolution).
    fsrMode: choiceSetting(input.fsrMode, ['native', 'quality', 'balanced', 'performance', 'ultraperformance'], 'quality'),
    // Master volume in percent; 100 leaves the game's sound as it is.
    volume,
    textures: input.textures !== false,
    // When HD textures load: while each level loads (stage), the whole pack over the first loading
    // screens and kept (all, for graphics cards with plenty of memory), or each when first drawn (play).
    textureLoading: choiceSetting(input.textureLoading, ['stage', 'all', 'play'], 'stage'),
    // The game compiles each level's shaders while it loads, and the rest in the background, instead
    // of when they are first drawn; on unless turned off.
    shaderWarmup: input.shaderWarmup !== false,
    // HD cutscenes are a separate 5 GB download, off unless chosen.
    cutscenes: Boolean(input.cutscenes),
    eclipse: Boolean(input.eclipse),
    autoUpdate: input.autoUpdate !== false,
    // Anonymous usage heartbeats (versions and OS only); see README Privacy.
    shareUsage: input.shareUsage !== false,
    // Show what you are playing on your Discord profile while the game runs.
    discordPresence: input.discordPresence !== false,
    // Keys the player changed in Settings → Controls; the rest stay the game's defaults.
    keyBindings: bindings.normalize(input.keyBindings),
    // Controller buttons the player changed in Settings → Controls.
    padBindings: bindings.normalizePads(input.padBindings),
    // How far the soft L / R bindings press the trigger, in percent (the game caps it below the click).
    softTrigger: numberSetting(input.softTrigger, 40, 5, 95),
    updateChannel: normalizeChannel(input.updateChannel)
  };
  // The graphics preset chosen (src/presets.js), while the picture settings still match it; else Custom.
  settings.graphicsPreset = presets.reconcile({ ...settings, graphicsPreset: input.graphicsPreset });
  return settings;
}

function cutscenePackDirectory(root) { return path.join(root, 'mods', 'hd-cutscenes'); }

function cutscenePackRequirements(root) {
  try {
    const catalog = JSON.parse(fs.readFileSync(path.join(root, 'tools', 'media', 'cutscene-release.json'), 'utf8'));
    if (catalog.schema !== 1 || catalog.movies.length !== 21) return null;
    let downloadBytes = 0, installedBytes = 0, largestPatch = 0;
    for (const movie of catalog.movies) {
      if (![movie.patch_bytes, movie.target_bytes].every(size => Number.isSafeInteger(size) && size > 0)) return null;
      downloadBytes += movie.patch_bytes;
      installedBytes += movie.target_bytes;
      largestPatch = Math.max(largestPatch, movie.patch_bytes);
    }
    return { downloadBytes, freeBytes: installedBytes + largestPatch + 964 * 1024 * 1024 };
  } catch (_) { return null; }
}

function cutscenePackInstalled(root) {
  const folder = cutscenePackDirectory(root);
  try {
    const release = fs.readFileSync(path.join(root, 'tools', 'media', 'cutscene-release.json'), 'utf8');
    const installed = fs.readFileSync(path.join(folder, 'installed.json'), 'utf8');
    const expected = JSON.parse(release), actual = JSON.parse(installed);
    if (expected.schema !== 1 || expected.movies.length !== 21 ||
        JSON.stringify(actual) !== JSON.stringify(expected) ||
        fs.readFileSync(path.join(folder, 'sms-hd-cutscenes-v1.complete'), 'utf8').trim() !== expected.release) return false;
    return expected.movies.every(movie => {
      const stat = fs.statSync(path.join(folder, 'files', movie.disc_path));
      return stat.isFile() && stat.size === movie.target_bytes;
    });
  } catch (_) { return false; }
}

// HD textures the UHD pack lacks, from the port's own release
// (chasem-dev/sms-hd-texture-extras). The port pins that release in
// tools/mods/texture-extras.json and records the one it installed in the
// install's .release file; a new pin means downloading only the extras again.
function textureExtrasDirectory(root) { return path.join(texturePackDirectory(root), 'sms-hd-texture-extras'); }

function textureExtrasSupported(root) {
  return fs.existsSync(path.join(root, 'tools', 'mods', 'texture-extras.json'));
}

function textureExtrasInstalled(root) {
  try {
    const pinned = JSON.parse(fs.readFileSync(path.join(root, 'tools', 'mods', 'texture-extras.json'), 'utf8'));
    const installed = JSON.parse(fs.readFileSync(path.join(textureExtrasDirectory(root), '.release'), 'utf8'));
    return typeof pinned.md5 === 'string' && /^[0-9a-f]{32}$/.test(pinned.md5) && installed.md5 === pinned.md5;
  } catch (_) { return false; }
}

// Extras to download for these settings: HD textures on, a port that knows
// them, and not the pinned release installed.
function textureExtrasWanted(root, settings) {
  return Boolean(settings.textures) && textureExtrasSupported(root) && !textureExtrasInstalled(root);
}

function cutscenePackSupported(root) {
  return ['install_cutscenes.py', 'cutscene-release.json'].some(file =>
    fs.existsSync(path.join(root, 'tools', 'media', file)));
}

// Settings from preferences.json. Before HD cutscenes had their own setting, HD
// textures brought them: players who already have the movie pack keep it.
function savedSettings(saved, repo) {
  if (!saved) return { textures: true };
  if ('cutscenes' in saved) return saved;
  return { ...saved, cutscenes: Boolean(saved.textures && repo && cutscenePackInstalled(repo)) };
}

// Eclipse plays its own movies, so HD cutscenes apply to Sunshine only. The
// game also only plays them with HD textures on (platform/thp/hd_movie_pack.cpp
// takes SMS_TEXTURE_PACKS=0 to mean no HD movies either).
function wantsCutscenes(settings) { return Boolean(settings.cutscenes && settings.textures && !settings.eclipse); }

// Every HD download the settings ask for is in place. Older playable
// installations (without the movie pack's files) keep playing until updated.
function hdVisualsInstalled(root, settings) {
  return (!settings.textures || texturePackInstalled(root)) &&
    (!wantsCutscenes(settings) || !cutscenePackSupported(root) || cutscenePackInstalled(root));
}

function texturePackDirectory(root) { return path.join(root, 'mods', 'textures'); }

function texturePackInstalled(root) {
  const pending = [texturePackDirectory(root)];
  while (pending.length) {
    let entries;
    const directory = pending.pop();
    try { entries = fs.readdirSync(directory, { withFileTypes: true }); }
    catch (error) { if (error.code === 'ENOENT') continue; throw error; }
    for (const entry of entries) {
      if (entry.isFile() && /^tex1_.*\.(?:dds|png)$/i.test(entry.name)) return true;
      if (entry.isDirectory()) pending.push(path.join(directory, entry.name));
    }
  }
  return false;
}

function buildEnvironment(settings, disc, root) {
  if (!root) throw new Error('The port folder is required to configure texture packs.');
  const env = {
    ...process.env,
    SMS_ARCH: settings.arch,
    SMS_ECLIPSE: settings.eclipse ? '1' : '0',
    SMS_DISC_IMAGE: disc,
    SMS_WIDESCREEN: settings.widescreen,
    SMS_WIDESCREEN_HUD: settings.hudEdges ? 'edges' : 'centre',
    SMS_FRAME_RATE: String(normalizeSettings(settings).frameRate),
    SMS_FULLSCREEN: settings.fullscreen ? (settings.fullscreenMode === 'exclusive' ? 'exclusive' : '1') : '0',
    SMS_VSYNC: settings.vsync === 'adaptive' ? 'adaptive' : settings.vsync === 'on' ? '1' : '0',
    SMS_SKIP_MOVIES: settings.skipMovies ? '1' : '0',
    SMS_SOFT_TRIGGER: String(settings.softTrigger ?? 40),
    SMS_HEAT_HAZE: settings.heatHaze === false ? '0' : '1',
    SMS_BUTTON_PROMPTS: prompts.normalizeStyle(settings.buttonPrompts),
    SMS_BUTTON_PROMPT_PAD: prompts.normalizePad(settings.promptPad),
    SMS_HDR: settings.hdr ? '1' : '0',
    ...(settings.hdr ? {
      SMS_HDR_PAPER_WHITE: settings.hdrCalibration === false ? String(settings.hdrPaperWhite ?? 200) : 'auto',
      SMS_HDR_PEAK: settings.hdrCalibration === false ? String(settings.hdrPeak ?? 1000) : 'auto',
      SMS_HDR_CONTRAST: String(settings.hdrContrast ?? 100),
      SMS_HDR_SATURATION: String(settings.hdrSaturation ?? 100),
      SMS_HDR_HIGHLIGHTS: String(settings.hdrHighlights ?? 40)
    } : {}),
    SMS_OVERLAY: settings.overlay ? '1' : '0',
    SMS_CAMERA_INVERT_X: settings.invertCameraX ? '1' : '0',
    SMS_CAMERA_INVERT_Y: settings.invertCameraY ? '1' : '0',
    SMS_FREE_CAMERA: settings.freeCamera ? '1' : '0',
    SMS_CAMERA_SPEED: String(settings.cameraSpeed ?? 100),
    SMS_MOUSE_CAMERA: settings.mouseCamera ? '1' : '0',
    SMS_MOUSE_SENSITIVITY: String(settings.mouseSensitivity ?? 100),
    SMS_MSAA: String(settings.msaa ?? 0),
    SMS_FXAA: settings.fxaa ? '1' : '0',
    SMS_ANISO: String(settings.anisotropic ?? 0),
    SMS_SHARPEN: String(settings.sharpen ?? 0),
    SMS_GAMMA: ((settings.brightness ?? 100) / 100).toFixed(2),
    SMS_ASPECT: settings.aspect || 'keep',
    SMS_PRESENT_FILTER: settings.presentFilter || 'bilinear',
    // the upscaling quality, for FSR 1 or NIS (NVIDIA Image Scaling)
    ...(['fsr', 'nis'].includes(settings.presentFilter) ? { SMS_FSR_MODE: settings.fsrMode || 'quality' } : {}),
    SMS_VOLUME: String(settings.volume ?? 100),
    SMS_GX_SCALE: String(settings.resolution),
    SMS_TEXTURE_PACKS: settings.textures ? texturePackDirectory(root) : '0',
    SMS_TEXTURE_PACK_PRELOAD: { all: 'all', play: '0' }[settings.textureLoading] || '1',
    SMS_GX_SHADER_WARMUP: settings.shaderWarmup === false ? '0' : '1',
    SMS_MOD: 'none',
    SMS_HD_CUTSCENES: wantsCutscenes(settings) ? cutscenePackDirectory(root) : '0'
  };
  if (settings.fullscreen && settings.fullscreenMode === 'exclusive' && /^\d+x\d+$/.test(settings.exclusiveResolution || ''))
    env.SMS_FULLSCREEN_MODE = settings.exclusiveResolution;
  if (settings.display === 'primary') env.SMS_DISPLAY = '0';
  return env;
}

function gameDisc(root, rom, eclipse) {
  if (!eclipse) return validateRom(rom);
  const modDisc = path.join(root, ECLIPSE_ISO);
  if (!fs.existsSync(modDisc)) throw new Error('Set up Eclipse using your own game disc file first.');
  return modDisc;
}

function binaryPath(root, settings, platform = process.platform) {
  const info = platformInfo(platform);
  const suffix = settings.eclipse ? '-eclipse' : '';
  return path.join(root, 'build', `${info.id}-${settings.arch}${suffix}`, platform === 'win32' ? 'sms.exe' : 'sms');
}

function playableInstall(saved, platform = process.platform) {
  if (!saved.repo || !saved.settings) return null;
  const settings = normalizeSettings(saved.settings, platform);
  if (!fs.existsSync(binaryPath(saved.repo, settings, platform))) return null;
  return { repo: saved.repo, settings, rom: saved.rom, saveDirectory: saved.saveDirectory || null };
}

// The MSYS shell's environment: the MinGW tools ahead of the Windows path.
function msysEnvironment(msys, environment) {
  const bins = environment.SMS_WINDOWS_32_CROSS ? []
    : [path.join(msys, environment.SMS_ARCH === '32' ? 'mingw32' : 'mingw64', 'bin'), path.join(msys, 'usr', 'bin')];
  return { ...prependWindowsPath(environment, bins),
    MSYSTEM: environment.SMS_ARCH === '32' && !environment.SMS_WINDOWS_32_CROSS ? 'MINGW32' : 'MINGW64', CHERE_INVOKING: '1' };
}

function commandFor(root, action, args = [], platform = process.platform, environment = process.env) {
  if (platform === 'win32' && action === 'run')
    return gameRunCommand(root, { arch: environment.SMS_ARCH || '64' }, args[0], platform, environment);
  const textureProgress = __dirname.includes('app.asar')
    ? path.join(process.resourcesPath, 'scripts', 'texture-progress.py')
    : path.resolve(__dirname, '..', 'scripts', 'texture-progress.py');
  if (platform !== 'win32') {
    const file = ['python', 'textures', 'texture-extras', 'cutscenes'].includes(action) ? 'python3' : path.join(root, `${action}.sh`);
    const commandArgs = action === 'python' ? ['tools/mods/get.py', 'eclipse', '--iso', ...args]
      : action === 'textures' ? [textureProgress, path.join(root, 'tools', 'mods', 'get.py')]
        : action === 'texture-extras' ? [textureProgress, path.join(root, 'tools', 'mods', 'get.py'), 'extras', '--if-outdated']
        : action === 'cutscenes' ? ['tools/media/install_cutscenes.py', '--iso', ...args] : args;
    return { command: file, args: commandArgs, cwd: root, env: environment };
  }
  const msys = environment.MSYS2_ROOT || 'C:\\msys64';
  const bash = path.join(msys, 'usr', 'bin', 'bash.exe');
  if (!fs.existsSync(bash)) throw new Error(`The Windows build tools are missing. Install it at ${msys} or set MSYS2_ROOT.`);
  const script = action === 'python'
    ? 'cd "$(cygpath -u "$1")" && python tools/mods/get.py eclipse --iso "$(cygpath -u "$2")"'
    : action === 'cutscenes'
      ? 'cd "$(cygpath -u "$1")" && python tools/media/install_cutscenes.py --iso "$(cygpath -u "$2")"'
    : action === 'textures'
      ? 'cd "$(cygpath -u "$1")" && python "$(cygpath -u "$2")" tools/mods/get.py'
    : action === 'texture-extras'
      ? 'cd "$(cygpath -u "$1")" && python "$(cygpath -u "$2")" tools/mods/get.py extras --if-outdated'
    : action === 'clean'
      ? 'cd "$(cygpath -u "$1")" && ./clean.sh "${@:2}"'
      : `cd "$(cygpath -u "$1")" && ./${action}.sh "$(cygpath -u "$2")"`;
  return {
    command: bash,
    args: ['-c', script, 'sms-launcher', root, ...(action === 'textures' || action === 'texture-extras' ? [textureProgress] : args)], cwd: root,
    env: msysEnvironment(msys, environment)
  };
}

function eclipseBuildCommand(root, settings, platform = process.platform, environment = process.env) {
  const script = __dirname.includes('app.asar')
    ? path.join(process.resourcesPath, 'scripts', 'build-eclipse.sh')
    : path.resolve(__dirname, '..', 'scripts', 'build-eclipse.sh');
  if (platform !== 'win32') return { command: script, args: [root, settings.arch], cwd: root, env: environment };
  const msys = environment.MSYS2_ROOT || 'C:\\msys64';
  const bash = path.join(msys, 'usr', 'bin', 'bash.exe');
  if (!fs.existsSync(bash)) throw new Error(`The Windows build tools are missing. Install it at ${msys} or set MSYS2_ROOT.`);
  return {
    command: bash,
    args: ['-c', 'exec "$(cygpath -u "$1")" "$(cygpath -u "$2")" "$3"', 'sms-launcher', script, root, settings.arch],
    cwd: root,
    env: msysEnvironment(msys, environment)
  };
}

// Spawn the Windows executable directly: MSYS bash translates many native
// exception statuses to 127, which hides the cause of a game crash.
function gameRunCommand(root, settings, disc, platform = process.platform, environment = process.env) {
  const binary = binaryPath(root, settings, platform);
  if (platform === 'linux' && settings.arch === '32' && environment.SMS_LINUX32_ROOT)
    return { command: path.join(environment.SMS_LINUX32_ROOT, 'i686-buildroot-linux-gnu', 'sysroot', 'lib', 'ld-linux.so.2'),
      args: ['--library-path', environment.SMS_LINUX32_LIBRARY_PATH, binary, disc], cwd: root,
      env: { ...environment, SMS_GAME_EXECUTABLE: binary } };
  if (platform !== 'win32') return { command: binary, args: [disc], cwd: root, env: environment };
  const msys = environment.MSYS2_ROOT || 'C:\\msys64';
  const runtime = settings.arch === '32' ? 'mingw32' : 'mingw64';
  const bins = settings.arch === '32' && environment.SMS_WINDOWS_32_CROSS
    ? [path.join(msys, 'opt', 'i686-w64-mingw32', 'bin'), path.join(msys, runtime, 'bin')]
    : [path.join(msys, runtime, 'bin')];
  return {
    command: binary, args: [disc], cwd: root,
    env: prependWindowsPath(environment, bins)
  };
}

function eclipseRunCommand(...args) { return gameRunCommand(...args); }

module.exports = { PORT_URL, ECLIPSE_ISO, platformInfo, isPort, validateRom, normalizeSettings, savedSettings,
  texturePackDirectory, texturePackInstalled, textureExtrasDirectory, textureExtrasSupported, textureExtrasInstalled, textureExtrasWanted,
  cutscenePackDirectory, cutscenePackInstalled, cutscenePackSupported, cutscenePackRequirements, wantsCutscenes, hdVisualsInstalled, buildEnvironment, gameDisc, binaryPath,
  commandFor, eclipseBuildCommand, eclipseRunCommand, playableInstall };
