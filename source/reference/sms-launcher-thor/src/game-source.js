'use strict';

// A game source other than this launcher release's: another sms-pc-port fork
// or commit, another decomp fork or commit, or both. Branches and tags are
// resolved to commits when chosen (and again by Update game), so each build
// knows exactly what it is made from.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const release = require('./game-release.json');

const SHA = /^[a-f0-9]{40}$/;
const REF = /^[A-Za-z0-9_][A-Za-z0-9._/-]{0,199}$/;
const REPOSITORY = /^(?:https:\/\/|ssh:\/\/|file:\/\/|git@[A-Za-z0-9.-]+:)[^\s]{1,400}$/;
// A row with only a branch, tag or commit means it is on our fork (the fields' placeholders).
const DEFAULT_REPOSITORY = { game: release.repository, decomp: 'https://github.com/chasem-dev/sms-english.git' };

function normalizePart(part, what) {
  if (!part) return null;
  const repository = String(part.repository || '').trim();
  const ref = String(part.ref || '').trim();
  if (!repository && !ref) return null;
  if (!repository) return normalizePart({ repository: DEFAULT_REPOSITORY[what], ref }, what);
  if (!REPOSITORY.test(repository) && !(path.isAbsolute(repository) && !repository.startsWith('-')))
    throw new Error(`Enter the ${what} repository as an https:// or git@ address, or a local folder.`);
  if (!REF.test(ref) || ref.includes('..') || ref.endsWith('/') || ref.endsWith('.lock'))
    throw new Error(`Enter a branch, tag or commit for the ${what}.`);
  return { repository, ref };
}

// Settings input -> { port, decomp } (each null for this release's), or null for no override.
function normalizeOverride(input) {
  if (!input) return null;
  const port = normalizePart(input.port, 'game');
  const decomp = normalizePart(input.decomp, 'decomp');
  return port || decomp ? { port, decomp } : null;
}

// `git ls-remote` output -> the commit a branch, tag or full ref names.
function pickRef(output, ref) {
  const refs = new Map(String(output).split('\n').map(line => line.trim().split(/\s+/)).filter(parts => SHA.test(parts[0]))
    .map(([sha, name]) => [name, sha]));
  for (const name of [`refs/heads/${ref}`, `refs/tags/${ref}^{}`, `refs/tags/${ref}`, ref])
    if (refs.has(name)) return { commit: refs.get(name), branch: name === `refs/heads/${ref}` ? ref : null };
  return null;
}

async function resolveRef({ repository, ref }, { git, capture, env }) {
  if (SHA.test(ref)) return { commit: ref, branch: null };
  const found = pickRef(await capture(git, ['ls-remote', repository, ref], undefined, env), ref);
  if (!found) throw new Error(`Could not find "${ref}" in ${repository}.`);
  return found;
}

// The decomp commit a port commit pins, from that one commit's trees.
async function pinnedDecomp(repository, commit, { git, capture, env }) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), `sms-source-${crypto.randomBytes(3).toString('hex')}-`));
  try {
    await capture(git, ['init', '-q', directory], undefined, env);
    await capture(git, ['fetch', '-q', '--depth', '1', '--filter=blob:none', repository, commit], directory, env);
    const entry = await capture(git, ['ls-tree', commit, 'decomp'], directory, env);
    const sha = entry.match(/^160000 commit ([a-f0-9]{40})\t/)?.[1];
    if (!sha) throw new Error('That game commit has no decomp submodule.');
    return sha;
  } finally { fs.rmSync(directory, { recursive: true, force: true }); }
}

// An override -> a source with this release's fields (see game-version.js).
async function resolve(override, { git = 'git', capture, env }) {
  if (!override) return release;
  // A private or mistyped repository must fail, not wait for a password nobody can type.
  const tools = { git, capture, env: { ...(env || process.env), GIT_TERMINAL_PROMPT: '0', GCM_INTERACTIVE: 'never' } };
  const port = override.port ? { repository: override.port.repository, ...await resolveRef(override.port, tools) }
    : { repository: release.repository, branch: release.branch, commit: release.commit };
  let decomp, decompRepository = null;
  if (override.decomp) {
    decomp = (await resolveRef(override.decomp, tools)).commit;
    decompRepository = override.decomp.repository;
  } else decomp = override.port ? await pinnedDecomp(port.repository, port.commit, tools) : release.decomp;
  return { custom: true, version: label(port.commit, override.decomp ? decomp : null),
    repository: port.repository, branch: port.branch, commit: port.commit, decomp, decompRepository, override };
}

function label(commit, decomp) {
  return `custom ${commit.slice(0, 7)}${decomp ? ` + decomp ${decomp.slice(0, 7)}` : ''}`;
}

module.exports = { normalizeOverride, pickRef, resolve };
