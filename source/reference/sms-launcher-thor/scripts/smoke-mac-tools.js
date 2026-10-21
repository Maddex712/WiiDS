'use strict';

// No ROM, game data, or compiled executable is uploaded by this test.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const buildTools = require('../src/build-tools');
const port = require('../src/port');
const game = require('../src/game-version');
const { execFileSync } = require('node:child_process');
const capture = async (command, args, cwd, env) => execFileSync(command, args, { cwd, env, encoding: 'utf8' }).trim();
const { run, runMain } = require('./tool-run');

async function main() {
  if (process.platform !== 'darwin') throw new Error('Run this smoke test on macOS.');
  // Reproduce an app opened from Finder, without Homebrew's shell initialization.
  const base = { ...process.env, PATH: '/usr/bin:/bin:/usr/sbin:/sbin', SMS_ARCH: '64' };
  delete base.HOMEBREW_PREFIX;
  delete base.CC;
  delete base.CXX;
  const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'sms Mac tools ci-'));
  try {
    const manifestFile = process.env.SMS_TOOL_ASSET_MANIFEST;
    const generated = manifestFile ? JSON.parse(fs.readFileSync(manifestFile, 'utf8')) : null;
    const source = generated?.platforms[process.arch] || null;
    if (source) {
      source.toolset ||= generated.toolset;
      require('../src/mac-tool-assets.json').platforms[process.arch] = source;
    }
    const archiveFile = source ? path.join(path.dirname(manifestFile), source.name) : null;
    await buildTools.prepare(userData, { forcePrivate: true, run, source, archiveFile, archives: true,
      progress(percent, detail) { if (detail) process.stdout.write(`${detail}\n`); }
    });
    const report = await buildTools.check(userData, { env: base, refresh: true, archives: true });
    if (!report.ready || report.mode !== 'private') throw new Error(report.message);
    process.stdout.write(`Private Mac preflight passed with Finder's PATH: ${base.PATH}\n`);
    const env = buildTools.environment(userData, base);
    env.PATH = `${env.SMS_BUILD_TOOLS_BIN}:/usr/bin:/bin:/usr/sbin:/sbin`;
    const root = path.join(userData, 'port');
    await game.checkout(root, { run, capture, env });
    try {
      await run(path.join(root, 'build.sh'), [], { cwd: root, env }, 'Compile Sunshine on Mac without a ROM');
    } catch (error) {
      // Finish independent compilation units to report every compiler error.
      // The original failed build still fails this test.
      const build = path.dirname(port.binaryPath(root, { arch: '64', eclipse: false }));
      if (fs.existsSync(path.join(build, 'build.ninja')))
        await run(path.join(env.SMS_BUILD_TOOLS_BIN, 'ninja'), ['-C', build, '-k', '0', 'sms'],
          { env }, 'Collect remaining Mac compiler errors').catch(() => {});
      throw error;
    }
    {
      const binary = port.binaryPath(root, { arch: '64', eclipse: false });
      if (!fs.existsSync(binary)) throw new Error(`Missing binary: ${binary}`);
      const cache = fs.readFileSync(path.join(path.dirname(binary), 'CMakeCache.txt'), 'utf8');
      if (!/^CMAKE_OSX_ARCHITECTURES:STRING=x86_64$/m.test(cache)) throw new Error('Mac build must target x86_64.');
      const compiler = cache.match(/^CMAKE_CXX_COMPILER:[^=]*=(.+)$/m)?.[1];
      if (!compiler?.startsWith(env.SMS_BUILD_TOOLS_BIN)) throw new Error(`Expected the Apple compiler wrapper: ${compiler}`);
      const wrapper = fs.readFileSync(compiler, 'utf8');
      if (!wrapper.includes('exec /usr/bin/clang++')) throw new Error('Mac builds must use the compiler from Apple Command Line Tools.');
      const objcopy = cache.match(/^SMS_OBJCOPY:[^=]*=(.+)$/m)?.[1];
      if (!objcopy?.startsWith(env.SMS_BUILD_TOOLS_BIN)) throw new Error(`Unexpected system LLVM tool: ${objcopy}`);
      await run('/usr/bin/arch', ['-x86_64', '/usr/bin/true'], { env }, 'Check Intel execution support');
      await run('/usr/bin/file', [binary], { env });
      const start = spawnSync(binary, [], { cwd: root, timeout: 15000, encoding: 'utf8',
        env: { ...env, SMS_HEADLESS: '1', SMS_SAVE_DIR: path.join(userData, 'test-saves'),
          SMS_DISC_IMAGE: path.join(userData, 'intentionally-missing.iso') } });
      if (start.error || start.signal || start.status !== 1 || !start.stderr.includes('not a usable GameCube disc image'))
        throw new Error(`The Mac executable did not reach the expected missing-disc check: ${start.error?.message || start.stderr}`);
      process.stdout.write('Mac executable loaded successfully and correctly requires a disc image.\n');
      const cmake = path.join(env.SMS_BUILD_TOOLS_BIN, 'cmake');
      await run(cmake, ['-S', root, '-B', path.dirname(binary), '-DSMS_GX_BUILD_TESTS=ON'], { cwd: root, env });
      await run(cmake, ['--build', path.dirname(binary), '--target', 'gx_windowtest', '--parallel', '2'], { cwd: root, env });
      await run(path.join(path.dirname(binary), 'platform', 'gx', 'gx_windowtest'), [], { cwd: root, env },
        'Verify game window placement on Mac');
      process.stdout.write(`Mac build succeeded: ${binary}\n`);
    }
  } finally { fs.rmSync(userData, { recursive: true, force: true }); }
}

runMain(main);
