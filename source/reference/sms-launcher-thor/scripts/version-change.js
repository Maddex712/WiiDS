'use strict';

const fs = require('node:fs');
const { execFileSync, spawnSync } = require('node:child_process');

const current = require('../package.json').version;
function parts(version) {
  if (!/^\d+\.\d+\.\d+$/.test(version)) throw new Error(`Use a stable X.Y.Z version: ${version}`);
  return version.split('.').map(Number);
}
function compare(a, b) {
  const left = parts(a), right = parts(b);
  for (let i = 0; i < 3; i++) if (left[i] !== right[i]) return Math.sign(left[i] - right[i]);
  return 0;
}
function previousVersion(before) {
  if (!/^[0-9a-f]{40}$/.test(before || '') || /^0{40}$/.test(before)) return null;
  try {
    const contents = execFileSync('git', ['show', `${before}:package.json`],
      { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
    return JSON.parse(contents).version || null;
  } catch (_) { return null; }
}

parts(current);
const previous = previousVersion(process.env.GITHUB_EVENT_BEFORE);
let changed = !previous || compare(current, previous) > 0;
if (previous && compare(current, previous) < 0) throw new Error(`Version went backwards: ${previous} → ${current}`);
// Serialized duplicate push events must not upload to an already public release.
// Drafts remain eligible so a failed release can be resumed.
if (changed && process.env.GITHUB_ACTIONS === 'true') {
  const release = spawnSync('gh', ['release', 'view', `v${current}`, '--repo', process.env.GITHUB_REPOSITORY,
    '--json', 'isDraft'], { encoding: 'utf8' });
  if (release.status === 0) changed = JSON.parse(release.stdout).isDraft;
  else if (release.error || !/release not found/i.test(release.stderr))
    throw new Error(`Cannot check the existing release: ${release.error?.message || release.stderr}`);
}
const output = `release=${changed}\ntag=v${current}\n`;
if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT, output);
console.log(previous ? `${previous} → ${current}: ${changed ? 'release' : 'no release'}` : `First version ${current}: release`);
