'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const changelog = require('../src/changelog');

const pin = { version: '2026.10.09.1', commit: 'a'.repeat(40), changes: ['Game fix'] };
const release = (version, extra = {}) => ({ version, date: '2026-10-09', launcher: [`Launcher ${version}`], game: pin, ...extra });

test('preview versions share the notes of the release they build on', () => {
  assert.equal(changelog.baseVersion('0.1.55-beta.25'), '0.1.55');
  assert.equal(changelog.baseVersion('0.1.55-pr12.3'), '0.1.55');
  assert.equal(changelog.baseVersion('0.1.55'), '0.1.55');
  for (const value of [undefined, null, '', 'v0.1.55', '1.2', 42]) assert.equal(changelog.baseVersion(value), null);
  assert.equal(changelog.compareVersions('0.1.10', '0.1.9'), 1);
  assert.equal(changelog.compareVersions('0.1.9', '0.2.0'), -1);
});

test('untrusted changelog JSON becomes plain releases, newest first, one per version', () => {
  const { releases } = changelog.normalize({ releases: [
    release('0.1.9'),
    release('0.1.10', { launcher: ['  Trimmed  ', 7, '', { html: '<b>' }], game: { ...pin, commit: 'not-a-commit' } }),
    release('0.1.10', { launcher: ['Duplicate'] }),
    { version: 'next', launcher: ['Bad version'] },
    { version: '0.1.8', launcher: [], game: { changes: [] } },
    null
  ] });
  assert.deepEqual(releases.map(item => item.version), ['0.1.10', '0.1.9']);
  assert.deepEqual(releases[0].launcher, ['Trimmed']);
  assert.equal(releases[0].game.commit, null);
  assert.deepEqual(releases[1].game, pin);
  assert.deepEqual(changelog.normalize('not json'), { releases: [], beta: null });
  assert.equal(changelog.normalize({ releases: [release('0.1.1', { date: 'yesterday' })] }).releases[0].date, null);
  assert.deepEqual(changelog.normalize({ beta: { launcher: [' New thing ', 3], game: 'not a list' } }).beta,
    { launcher: ['New thing'], game: [] });
  assert.equal(changelog.normalize({ beta: { launcher: [], game: [] } }).beta, null);
});

test('the published changelog corrects bundled notes, keeps versions only bundled, and owns Beta', () => {
  const merged = changelog.merge(changelog.normalize({ releases: [release('0.1.2', { launcher: ['Corrected'] })] }),
    changelog.normalize({ beta: { launcher: ['Folded into a release'] }, releases: [release('0.1.3'), release('0.1.2', { launcher: ['Typo'] })] }));
  assert.deepEqual(merged.releases.map(item => [item.version, item.launcher[0]]), [['0.1.3', 'Launcher 0.1.3'], ['0.1.2', 'Corrected']]);
  assert.equal(merged.beta, null, "Beta's notes follow main, even when emptied");
});

test("Beta builds show Beta's notes once per wording", () => {
  const beta = { launcher: ['Beta change'], game: [] };
  const key = changelog.betaKey(beta);
  assert.match(key, /^[a-f0-9]{16}$/);
  assert.notEqual(changelog.betaKey({ launcher: ['Beta change, reworded'], game: [] }), key);
  assert.equal(changelog.isBeta('0.1.55-beta.25'), true);
  for (const version of ['0.1.55', '0.1.55-pr12.3', undefined]) assert.equal(changelog.isBeta(version), false);
  const show = options => changelog.unseenBeta(beta, { current: '0.1.55-beta.25', returning: true, ...options });
  assert.equal(show({ seen: null }), true);
  assert.equal(show({ seen: '0'.repeat(16) }), true, 'notes changed since last shown');
  assert.equal(show({ seen: key }), false);
  assert.equal(show({ seen: null, returning: false }), false, 'a new install has nothing to catch up on');
  assert.equal(show({ seen: null, current: '0.1.55' }), false, 'stable builds never show Beta notes');
  assert.equal(changelog.unseenBeta(null, { seen: null, current: '0.1.55-beta.1', returning: true }), false);
});

test('after an update the releases since the last one seen show once', () => {
  const { releases } = changelog.normalize({ releases: ['0.1.5', '0.1.4', '0.1.3', '0.1.2'].map(version => release(version)) });
  const unseen = options => changelog.unseen(releases, options).map(item => item.version);
  assert.deepEqual(unseen({ seen: '0.1.2', current: '0.1.4' }), ['0.1.4', '0.1.3']);
  assert.deepEqual(unseen({ seen: '0.1.4', current: '0.1.4' }), []);
  assert.deepEqual(unseen({ seen: '0.1.4', current: '0.1.3' }), [], 'going back to an older release shows nothing');
  assert.deepEqual(unseen({ seen: '0.1.3', current: '0.1.4-beta.7' }), ['0.1.4']);
  assert.deepEqual(unseen({ seen: null, current: '0.1.4', returning: false }), [], 'a new install has nothing to catch up on');
  assert.deepEqual(unseen({ seen: null, current: '0.1.4', returning: true }), ['0.1.4'], 'players from before the changelog see this release');
  assert.deepEqual(unseen({ seen: '0.1.2', current: 'dev' }), []);
});

test("this repository's changelog describes the current launcher release and its game pin", () => {
  const root = path.join(__dirname, '..');
  const raw = JSON.parse(fs.readFileSync(path.join(root, changelog.FILE), 'utf8'));
  const { releases } = changelog.normalize(raw);
  assert.equal(releases.length, raw.releases.length, 'every entry needs an X.Y.Z version, a unique version and at least one change');
  assert.deepEqual(raw.releases.map(item => item.version), releases.map(item => item.version), 'list the newest release first');
  for (const item of raw.releases) {
    assert.match(item.date, /^\d{4}-\d{2}-\d{2}$/, `v${item.version} needs a date`);
    assert.ok(item.game?.version && /^[a-f0-9]{40}$/.test(item.game.commit), `v${item.version} needs its game pin`);
  }
  const pkg = require('../package.json');
  const entry = releases.find(item => item.version === pkg.version);
  assert.ok(entry, `Add v${pkg.version} to changelog.json: npm run update:game drafts it, and AGENTS.md explains a launcher-only bump`);
  const game = require('../src/game-release.json');
  assert.deepEqual([entry.game.version, entry.game.commit], [game.version, game.commit],
    `changelog.json v${pkg.version} must list the game pinned in src/game-release.json`);
  assert.ok(pkg.build.files.includes(changelog.FILE), 'package the changelog for offline launchers');
});
