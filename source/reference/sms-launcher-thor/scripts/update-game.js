'use strict';

// Maintainer command: select a complete port revision, then bump the launcher
// so its release gate verifies exactly the source users will receive.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { execFileSync } = require('node:child_process');
const current = require('../src/game-release.json');
const ref = process.argv[2] || `origin/${current.branch}`;
const launcherRoot = path.join(__dirname, '..');
const previousLauncher = require('../package.json').version;
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-game-release-'));
const git = args => execFileSync('git', args, { cwd: root, encoding: 'utf8' }).trim();

// Commit subjects are a draft: the maintainer rewrites them for players.
function subjects(args, cwd) {
  try {
    return execFileSync('git', ['log', '--no-merges', '--format=%s', ...args], { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] })
      .split('\n').map(line => line.replace(/\s*\(#\d+\)$/, '').trim()).filter(Boolean);
  } catch (_) { return []; }
}

// The launcher release that ships a game pin lists the game's changes too.
// Beta's hand-written notes become the release's, and Beta starts empty again;
// without them, commit subjects are the draft.
function addChangelogEntry(version, game, previousCommit) {
  const file = path.join(launcherRoot, 'changelog.json');
  const changelog = JSON.parse(fs.readFileSync(file, 'utf8'));
  const existing = changelog.releases.find(release => release.version === version);
  const beta = { launcher: changelog.beta?.launcher || [], game: changelog.beta?.game || [] };
  const launcher = existing?.launcher?.length ? existing.launcher : beta.launcher.length ? beta.launcher
    : subjects([`v${previousLauncher}..HEAD`], launcherRoot).filter(line => !/^(Select game|Bump launcher)/.test(line));
  const entry = { version, date: new Date().toISOString().slice(0, 10), launcher,
    game: { version: game.version, commit: game.commit,
      changes: beta.game.length ? beta.game : subjects([`${previousCommit}..${game.commit}`], root) } };
  changelog.beta = { launcher: [], game: [] };
  changelog.releases = [entry, ...changelog.releases.filter(release => release.version !== version)];
  fs.writeFileSync(file, `${JSON.stringify(changelog, null, 2)}\n`);
  return beta.launcher.length || beta.game.length;
}

try {
  git(['clone', '--filter=blob:none', '--no-checkout', '--branch', current.branch, current.repository, '.']);
  const commit = git(['rev-parse', '--verify', `${ref}^{commit}`]);
  const decomp = git(['ls-tree', commit, 'decomp']).match(/^160000 commit ([a-f0-9]{40})\tdecomp$/)?.[1];
  if (!decomp) throw new Error('This port revision does not pin its decomp submodule.');
  if (commit === current.commit && decomp === current.decomp) {
    console.log('This game revision is already selected. No release bump needed.');
  } else {
    const date = new Date().toISOString().slice(0, 10).replaceAll('-', '.');
    const number = current.version.startsWith(`${date}.`) ? Number(current.version.split('.').at(-1)) + 1 : 1;
    const next = { ...current, version: `${date}.${number}`, commit, decomp };
    fs.writeFileSync(path.join(__dirname, '..', 'src', 'game-release.json'), `${JSON.stringify(next, null, 2)}\n`);
    execFileSync(process.platform === 'win32' ? 'npm.cmd' : 'npm', ['version', 'patch', '--no-git-tag-version'],
      { cwd: path.join(__dirname, '..'), stdio: 'inherit', shell: process.platform === 'win32' });
    const version = JSON.parse(fs.readFileSync(path.join(launcherRoot, 'package.json'), 'utf8')).version;
    const fromBeta = addChangelogEntry(version, next, current.commit);
    console.log(`Selected game ${next.version}: ${commit} (decomp ${decomp}).`);
    console.log(fromBeta
      ? `Moved Beta's notes into changelog.json v${version}. Check they match this game commit, then commit and push to run the release checks.`
      : `Drafted changelog.json notes for v${version} from commit subjects. Edit them for players, then commit and push to run the release checks.`);
  }
} finally { fs.rmSync(root, { recursive: true, force: true }); }
