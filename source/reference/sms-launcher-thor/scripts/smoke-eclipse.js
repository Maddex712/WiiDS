'use strict';

// Build the Eclipse game target with the private tools, the way the launcher does.
// No disc image or compiled game is uploaded by this check.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync, spawnSync } = require('node:child_process');
const buildTools = require('../src/build-tools');
const port = require('../src/port');
const game = require('../src/game-version');
const capture = async (command, args, cwd, env) => execFileSync(command, args, { cwd, env, encoding: 'utf8' }).trim();
const { run, runMain } = require('./tool-run');

async function main() {
  const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'sms eclipse ci-'));
  const manifestFile = process.env.SMS_TOOL_ASSET_MANIFEST;
  const generated = manifestFile ? JSON.parse(fs.readFileSync(manifestFile, 'utf8')) : null;
  const source = generated?.platforms[process.platform === 'darwin' ? process.arch : process.platform] || null;
  if (source) {
    source.toolset ||= generated.toolset;
    const manifest = require(process.platform === 'darwin' ? '../src/mac-tool-assets.json' : '../src/tool-assets.json');
    manifest.platforms[process.platform === 'darwin' ? process.arch : process.platform] = source;
  }
  const archiveFile = source ? path.join(path.dirname(manifestFile), source.name) : null;
  // Only the private tools (and, on Mac, Apple's) may be found, as for a launcher opened from the desktop.
  const base = { ...process.env, SMS_ARCH: '64', PATH: process.platform === 'win32'
    ? path.join(process.env.SystemRoot, 'System32') : process.platform === 'darwin' ? '/usr/bin:/bin:/usr/sbin:/sbin' : '' };
  for (const name of ['HOMEBREW_PREFIX', 'CC', 'CXX']) delete base[name];
  await buildTools.prepare(userData, { forcePrivate: true, run, archives: true, source, archiveFile,
    progress(percent, detail) { if (detail) process.stdout.write(`${detail}\n`); }
  });
  const env = buildTools.environment(userData, base);
  if (process.platform === 'darwin') env.PATH = `${env.SMS_BUILD_TOOLS_BIN}:/usr/bin:/bin:/usr/sbin:/sbin`;
  const root = path.join(userData, 'port');
  await game.checkout(root, { run, capture, env });
  const settings = { arch: '64', eclipse: true };
  const cmd = port.eclipseBuildCommand(root, settings, process.platform, { ...env, NINJA_STATUS: '[%f/%t] ' });
  await run(cmd.command, cmd.args, { cwd: cmd.cwd, env: cmd.env }, `Build Eclipse using private ${process.platform} tools`);
  const binary = port.binaryPath(root, settings);
  if (!fs.existsSync(binary)) throw new Error(`Eclipse build completed without ${binary}`);
  const invalidDisc = path.join(userData, 'intentionally-invalid.iso');
  fs.writeFileSync(invalidDisc, Buffer.alloc(32));
  const launch = port.eclipseRunCommand(root, settings, invalidDisc, process.platform,
    { ...env, SMS_CODE_MODS: '0', SMS_HEADLESS: '1', SMS_SAVE_DIR: path.join(userData, 'test-saves'), SMS_DISC_IMAGE: invalidDisc });
  const start = spawnSync(launch.command, launch.args, { cwd: root, timeout: 45000, encoding: 'utf8', windowsHide: true, env: launch.env });
  if (start.error || start.signal || start.status !== 1 || !start.stderr.includes('not a usable GameCube disc image') ||
      !`${start.stdout}${start.stderr}`.includes('code mods linked in but switched off'))
    throw new Error(`Eclipse game did not reach the disc check: ${JSON.stringify({
      error: start.error?.message, status: start.status, signal: start.signal, stdout: start.stdout, stderr: start.stderr })}`);
  process.stdout.write(`Private tools compiled and started the Eclipse game: ${binary}\n`);
}

runMain(main);
