'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const game = require('../src/game-version');
const port = require('../src/port');
const tools = require('../src/build-tools');
const settings = port.normalizeSettings();

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms game version-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  for (const [folder, commit] of [['', game.release.commit], ['decomp', game.release.decomp]]) {
    fs.mkdirSync(path.join(root, folder, '.git'), { recursive: true });
    fs.writeFileSync(path.join(root, folder, '.git', 'HEAD'), `${commit}\n`);
  }
  const binary = port.binaryPath(root, settings);
  fs.mkdirSync(path.dirname(binary), { recursive: true });
  fs.writeFileSync(binary, 'previous playable game');
  return { root, binary, directory: path.dirname(binary), backups: path.join(root, 'backups') };
}

test('legacy builds require one verified update; display settings and launcher versions do not require rebuilds', t => {
  const { root } = fixture(t);
  assert.equal(Boolean(game.isCurrent(root, settings)), false);
  fs.writeFileSync(path.join(path.dirname(port.binaryPath(root, settings)), 'launcher-build.json'),
    JSON.stringify({ ...game.identity(settings), launcherVersion: '0.0.1' }));
  assert.equal(game.isCurrent(root, { ...settings, resolution: 4, frameRate: 120, textures: true, fullscreen: true }), true);
  const marker = path.join(path.dirname(port.binaryPath(root, settings)), 'launcher-build.json');
  fs.writeFileSync(marker, JSON.stringify({ ...game.identity(settings), toolSha256: '0'.repeat(64) }));
  assert.equal(game.isCurrent(root, settings), false);
});

test('changed source and decomp revisions invalidate a build independently', t => {
  const { root, directory } = fixture(t);
  fs.writeFileSync(path.join(directory, 'launcher-build.json'), JSON.stringify(game.identity(settings)));
  fs.writeFileSync(path.join(root, 'decomp', '.git', 'HEAD'), '0'.repeat(40));
  assert.equal(game.isCurrent(root, settings), false);
  fs.writeFileSync(path.join(root, 'decomp', '.git', 'HEAD'), game.release.decomp);
  fs.writeFileSync(path.join(root, '.git', 'HEAD'), '1'.repeat(40));
  assert.equal(game.isCurrent(root, settings), false);
});

test('failed rebuild restores the playable binary, metadata, and custom saves inside its build folder', async t => {
  const { root, binary, directory, backups } = fixture(t);
  const card = path.join(directory, 'card');
  fs.mkdirSync(card);
  fs.writeFileSync(path.join(card, 'index.txt'), 'all my progress');
  fs.writeFileSync(path.join(directory, 'launcher-build.json'), '{"gameVersion":"old"}');
  await assert.rejects(game.buildSafely(root, settings, card, async () => {
    fs.writeFileSync(binary, 'partial executable');
    throw new Error('compiler failed');
  }, backups), /compiler failed/);
  assert.equal(fs.readFileSync(binary, 'utf8'), 'previous playable game');
  assert.equal(fs.readFileSync(path.join(card, 'index.txt'), 'utf8'), 'all my progress');
  assert.equal(game.installed(root, settings).gameVersion, 'old');
  assert.equal(fs.existsSync(`${directory}.launcher-transaction.json`), false);
});

test('successful build records exact revisions and preserves saves', async t => {
  const { root, binary, directory, backups } = fixture(t);
  const card = path.join(directory, 'card');
  fs.mkdirSync(card);
  fs.writeFileSync(path.join(card, 'index.txt'), 'my saves');
  await game.buildSafely(root, settings, card, async () => {
    fs.writeFileSync(binary, 'new playable game');
  }, backups);
  assert.equal(game.isCurrent(root, settings), true);
  assert.equal(fs.readFileSync(path.join(card, 'index.txt'), 'utf8'), 'my saves');
});

test('setup interrupted by closing the app recovers its earlier playable build on next start', t => {
  const { root, binary, directory } = fixture(t);
  const previous = `${directory}.previous-fixture`;
  fs.writeFileSync(`${directory}.launcher-transaction.json`, JSON.stringify({ directory, previous }));
  fs.renameSync(directory, previous);
  fs.mkdirSync(directory);
  fs.writeFileSync(binary, 'incomplete output');
  assert.equal(game.recoverBuild(root, settings), true);
  assert.equal(fs.readFileSync(binary, 'utf8'), 'previous playable game');
  assert.equal(game.recoverBuild(root, settings), false);
});

test('new source snapshots share optional downloads and keep user settings in the original folder', t => {
  const { root } = fixture(t);
  const next = path.join(root, 'next');
  fs.mkdirSync(path.join(next, 'mods'), { recursive: true });
  fs.mkdirSync(path.join(root, 'mods', 'textures'), { recursive: true });
  fs.writeFileSync(path.join(root, 'mods', 'textures', 'tex1_test.png'), 'textures');
  fs.mkdirSync(path.join(root, 'mods', 'hd-cutscenes', 'files', 'data'), { recursive: true });
  fs.writeFileSync(path.join(root, 'mods', 'hd-cutscenes', 'files', 'data', 'openingA.thp'), 'installed HD movie');
  fs.writeFileSync(path.join(root, 'settings.txt'), 'save_dir=./custom-saves');
  game.carryUserFiles(root, next);
  assert.equal(fs.readFileSync(path.join(next, 'settings.txt'), 'utf8'), 'save_dir=./custom-saves');
  assert.equal(fs.realpathSync(path.join(next, 'mods', 'textures')), fs.realpathSync(path.join(root, 'mods', 'textures')));
  assert.equal(fs.realpathSync(path.join(next, 'mods', 'hd-cutscenes')), fs.realpathSync(path.join(root, 'mods', 'hd-cutscenes')));
  assert.equal(fs.readFileSync(path.join(next, 'mods', 'hd-cutscenes', 'files', 'data', 'openingA.thp'), 'utf8'), 'installed HD movie');
  assert.equal(fs.readFileSync(path.join(root, 'settings.txt'), 'utf8'), 'save_dir=./custom-saves');
});

test('each OS and Mac architecture has its own tool version without changing existing archive hashes', () => {
  for (const [platform, arch] of [['linux','x64'],['win32','x64'],['darwin','x64'],['darwin','arm64']])
    assert.equal(tools.toolsetFor(platform, arch), tools.assetFor(platform, arch).toolset);
});

test('download selects exact port and decomp commits even when the branch has newer changes', async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms checkout pin-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const env = { ...process.env, GIT_CONFIG_COUNT: '1', GIT_CONFIG_KEY_0: 'protocol.file.allow', GIT_CONFIG_VALUE_0: 'always' };
  const git = (cwd, args) => execFileSync('git', args, { cwd, env, encoding: 'utf8', stdio: ['ignore','pipe','pipe'] }).trim();
  function repo(name) {
    const dir = path.join(root, name); fs.mkdirSync(dir);
    git(dir, ['init', '-b', game.release.branch]);
    git(dir, ['config','user.name','CI']); git(dir, ['config','user.email','ci@example.invalid']);
    fs.writeFileSync(path.join(dir,'file.txt'),'one'); git(dir,['add','.']); git(dir,['commit','-m','first']);
    return dir;
  }
  const decomp = repo('decomp');
  const origin = repo('origin');
  git(origin, ['submodule','add',decomp,'decomp']); git(origin,['commit','-am','pin decomp']);
  const source = { ...game.release, repository: origin, commit: git(origin,['rev-parse','HEAD']), decomp: git(decomp,['rev-parse','HEAD']) };
  fs.writeFileSync(path.join(origin,'file.txt'),'newer'); git(origin,['commit','-am','later untested change']);
  const capture = async (command,args,cwd,environment) => execFileSync(command,args,{cwd,env:environment,encoding:'utf8',stdio:['ignore','pipe','pipe']}).trim();
  const run = async (command,args,options) => capture(command,args,options.cwd,options.env);
  const destination = path.join(root,'installed');
  await game.checkout(destination, { source, env, run, capture });
  assert.equal(game.gitRevision(destination), source.commit);
  assert.equal(game.gitRevision(path.join(destination,'decomp')), source.decomp);
  assert.equal(git(destination, ['submodule', 'status']).trim().slice(0, 40), source.decomp);
  assert.equal(fs.readFileSync(path.join(destination,'file.txt'),'utf8'),'one');
  const failed = path.join(root,'failed');
  await assert.rejects(game.checkout(failed, { source: { ...source, decomp:'0'.repeat(40) }, env, run, capture }), /did not match/);
  assert.equal(fs.existsSync(failed),false);
  assert.equal(fs.readdirSync(root).some(name => name.startsWith('failed.download-')),false);
});

test('a decomp from another fork and commit survives the port\'s own submodule update', async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms checkout fork-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const env = { ...process.env, GIT_CONFIG_COUNT: '1', GIT_CONFIG_KEY_0: 'protocol.file.allow', GIT_CONFIG_VALUE_0: 'always' };
  const git = (cwd, args) => execFileSync('git', args, { cwd, env, encoding: 'utf8', stdio: ['ignore','pipe','pipe'] }).trim();
  function repo(name) {
    const dir = path.join(root, name); fs.mkdirSync(dir);
    git(dir, ['init', '-b', game.release.branch]);
    git(dir, ['config','user.name','CI']); git(dir, ['config','user.email','ci@example.invalid']);
    fs.writeFileSync(path.join(dir,'file.txt'),'one'); git(dir,['add','.']); git(dir,['commit','-m','first']);
    return dir;
  }
  const decomp = repo('decomp');
  const origin = repo('origin');
  git(origin, ['submodule','add',decomp,'decomp']); git(origin,['commit','-am','pin decomp']);
  // The fork has a commit the port does not pin.
  const fork = path.join(root, 'fork');
  git(root, ['clone', '-q', decomp, fork]);
  git(fork, ['config','user.name','CI']); git(fork, ['config','user.email','ci@example.invalid']);
  fs.writeFileSync(path.join(fork,'file.txt'),'fork'); git(fork,['commit','-qam','fork change']);
  const source = { ...game.release, custom: true, repository: origin, commit: git(origin,['rev-parse','HEAD']),
    decomp: git(fork,['rev-parse','HEAD']), decompRepository: fork };
  const capture = async (command,args,cwd,environment) => execFileSync(command,args,{cwd,env:environment,encoding:'utf8',stdio:['ignore','pipe','pipe']}).trim();
  const run = async (command,args,options) => capture(command,args,options.cwd,options.env);
  const destination = path.join(root,'installed');
  await game.checkout(destination, { source, env, run, capture });
  assert.equal(game.matchesSource(destination, source), true);
  assert.equal(fs.readFileSync(path.join(destination,'decomp','file.txt'),'utf8'),'fork');
  git(destination, ['submodule', 'update', '--init', 'decomp']);  // what build.sh runs
  assert.equal(game.gitRevision(path.join(destination,'decomp')), source.decomp);
  assert.notEqual(game.snapshotPath('/x/sms', { arch: '64', eclipse: false }, source),
    game.snapshotPath('/x/sms', { arch: '64', eclipse: false }));
});

test('build records the matching tool location and requests preparation again if those tools are removed', async t => {
  const { root, directory, binary, backups } = fixture(t);
  const toolRoot = path.join(root, 'private-tools');
  fs.mkdirSync(toolRoot);
  await game.buildSafely(root, settings, path.join(root, 'saves'), async () => {
    fs.mkdirSync(directory, { recursive: true });
    fs.writeFileSync(binary, 'new executable');
    fs.writeFileSync(path.join(directory, 'CMakeCache.txt'), `CMAKE_CXX_COMPILER:FILEPATH=${toolRoot.replaceAll('\\','/')}/env/bin/clang++\n`);
  }, backups);
  assert.equal(game.installed(root, settings).toolRoot, toolRoot.replaceAll('\\','/'));
  assert.equal(game.isCurrent(root, settings), true);
  fs.rmSync(toolRoot, { recursive: true });
  assert.equal(game.isCurrent(root, settings), false);
});

test('records the toolset root for x64-host cross compilers as well as native compilers', t => {
  const { root, directory } = fixture(t);
  for (const suffix of ['env/bin/g++', 'env/targets/linux32/bin/i686-linux-g++',
    'msys64/opt/bin/i686-w64-mingw32-g++.exe', 'msys64/mingw64/bin/g++.exe']) {
    fs.writeFileSync(path.join(directory, 'CMakeCache.txt'), `CMAKE_CXX_COMPILER:FILEPATH=/private/tools/${suffix}\n`);
    assert.equal(game.compilerToolRoot(root, settings), '/private/tools');
  }
});
