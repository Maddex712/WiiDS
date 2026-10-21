'use strict';

const crypto = require('node:crypto');

// The changelog lives in changelog.json at the top of the launcher repository.
// Each entry is one launcher release and the game it pins, so a game update
// is described by the launcher bump that ships it. Packaged launchers read the
// file from GitHub, and fall back to the copy they were built with.
// Its "beta" section is hand-written notes for what Beta has that no release
// has yet: newer launcher main and the newest game commit. Only Beta builds
// show it, and update:game moves it into the next release's entry.
const REPOSITORY = 'chasem-dev/sms-launcher';
const BRANCH = 'main';
const FILE = 'changelog.json';
const PAGE_URL = `https://github.com/${REPOSITORY}/blob/${BRANCH}/${FILE}`;
const VERSION = /^\d+\.\d+\.\d+$/;
const DATE = /^\d{4}-\d{2}-\d{2}$/;
const MAX_TEXT = 400;
const MAX_ITEMS = 60;

function url(env = process.env) {
  return env.SMS_CHANGELOG_URL || `https://raw.githubusercontent.com/${REPOSITORY}/${BRANCH}/${FILE}`;
}

// "0.1.55-beta.25" -> "0.1.55": previews share the notes of the version they build on.
function baseVersion(version) {
  const base = String(version || '').split(/[-+]/)[0];
  return VERSION.test(base) ? base : null;
}

function compareVersions(a, b) {
  const left = a.split('.').map(Number), right = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) if (left[i] !== right[i]) return Math.sign(left[i] - right[i]);
  return 0;
}

function changes(list) {
  return (Array.isArray(list) ? list : [])
    .filter(item => typeof item === 'string' && item.trim())
    .slice(0, MAX_ITEMS).map(item => item.trim().slice(0, MAX_TEXT));
}

function normalizeRelease(entry) {
  if (!entry || typeof entry !== 'object' || !VERSION.test(entry.version)) return null;
  const game = entry.game && typeof entry.game === 'object' ? {
    version: typeof entry.game.version === 'string' ? entry.game.version.slice(0, 40) : null,
    commit: /^[a-f0-9]{40}$/.test(entry.game.commit) ? entry.game.commit : null,
    changes: changes(entry.game.changes)
  } : null;
  const release = { version: entry.version, date: DATE.test(entry.date) ? entry.date : null,
    title: typeof entry.title === 'string' ? entry.title.trim().slice(0, 120) : '',
    launcher: changes(entry.launcher), game };
  return release.launcher.length || game?.changes.length ? release : null;
}

function normalizeBeta(beta) {
  if (!beta || typeof beta !== 'object') return null;
  const notes = { launcher: changes(beta.launcher), game: changes(beta.game) };
  return notes.launcher.length || notes.game.length ? notes : null;
}

// Untrusted JSON -> releases, newest first, one per version, and Beta's notes.
function normalize(data) {
  return { releases: normalizeReleases(data), beta: normalizeBeta(data?.beta) };
}

function normalizeReleases(data) {
  const seen = new Set();
  const releases = [];
  for (const entry of Array.isArray(data?.releases) ? data.releases : []) {
    const release = normalizeRelease(entry);
    if (!release || seen.has(release.version)) continue;
    seen.add(release.version);
    releases.push(release);
  }
  return releases.sort((a, b) => compareVersions(b.version, a.version));
}

// The published file wins for each version it has, so notes can be corrected
// after a release; versions only in the bundled copy (a newer preview) stay.
// Beta's notes always come from the published file: they follow main.
function merge(published, bundled) {
  const versions = new Set(published.releases.map(release => release.version));
  return { beta: published.beta, releases: published.releases
    .concat(bundled.releases.filter(release => !versions.has(release.version)))
    .sort((a, b) => compareVersions(b.version, a.version)) };
}

function isBeta(version) { return /^\d+\.\d+\.\d+-beta\./.test(String(version || '')); }

// Identifies one wording of Beta's notes, so they show again only when they change.
function betaKey(beta) {
  return beta ? crypto.createHash('sha256').update(JSON.stringify([beta.launcher, beta.game])).digest('hex').slice(0, 16) : null;
}

// Releases to show once after an update: newer than the last version the
// player saw, up to the running one. A new install has nothing to catch up
// on. A player from before the changelog existed sees this release's notes.
function unseen(releases, { seen, current, returning }) {
  current = baseVersion(current);
  if (!current) return [];
  seen = baseVersion(seen);
  if (!seen) return returning ? releases.filter(release => release.version === current) : [];
  return releases.filter(release =>
    compareVersions(release.version, seen) > 0 && compareVersions(release.version, current) <= 0);
}

// Beta builds show Beta's notes once each time they change. Like releases, a
// new install starts with nothing to catch up on.
function unseenBeta(beta, { seen, current, returning }) {
  return Boolean(isBeta(current) && beta && betaKey(beta) !== seen && (seen || returning));
}

module.exports = { REPOSITORY, FILE, PAGE_URL, url, baseVersion, compareVersions, normalize, merge, unseen,
  isBeta, betaKey, unseenBeta };
