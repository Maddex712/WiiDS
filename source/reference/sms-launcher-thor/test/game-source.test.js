'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const source = require('../src/game-source');
const release = require('../src/game-release.json');

const capture = async (command, args, cwd, env) => execFileSync(command, args, { cwd, env, encoding: 'utf8' }).trim();
const git = (cwd, args) => execFileSync('git', args, { cwd, encoding: 'utf8',
  env: { ...process.env, GIT_AUTHOR_NAME: 't', GIT_AUTHOR_EMAIL: 't@t', GIT_COMMITTER_NAME: 't', GIT_COMMITTER_EMAIL: 't@t' } }).trim();

test('an empty override is this release; bad repositories and refs are refused', () => {
  assert.equal(source.normalizeOverride(null), null);
  assert.equal(source.normalizeOverride({ port: { repository: '', ref: '' }, decomp: {} }), null);
  assert.deepEqual(source.normalizeOverride({ decomp: { repository: ' https://github.com/me/sms-english.git ', ref: 'fix/thing' } }),
    { port: null, decomp: { repository: 'https://github.com/me/sms-english.git', ref: 'fix/thing' } });
  assert.deepEqual(source.normalizeOverride({ port: { repository: ' ', ref: 'fix/thing' }, decomp: { ref: 'abc123' } }),
    { port: { repository: release.repository, ref: 'fix/thing' },
      decomp: { repository: 'https://github.com/chasem-dev/sms-english.git', ref: 'abc123' } });
  assert.throws(() => source.normalizeOverride({ port: { repository: '', ref: '-x' } }), /branch/);
  assert.throws(() => source.normalizeOverride({ port: { repository: '--upload-pack=x', ref: 'main' } }), /repository/);
  assert.throws(() => source.normalizeOverride({ port: { repository: 'https://github.com/me/x', ref: '-x' } }), /branch/);
  assert.throws(() => source.normalizeOverride({ port: { repository: 'https://github.com/me/x', ref: 'a..b' } }), /branch/);
  assert.throws(() => source.normalizeOverride({ port: { repository: 'https://github.com/me/x', ref: '' } }), /branch/);
});

test('picks a branch, then a peeled tag, from ls-remote', () => {
  const a = 'a'.repeat(40), b = 'b'.repeat(40), c = 'c'.repeat(40);
  const out = `${a}\trefs/heads/main\n${b}\trefs/tags/v1\n${c}\trefs/tags/v1^{}\n`;
  assert.deepEqual(source.pickRef(out, 'main'), { commit: a, branch: 'main' });
  assert.deepEqual(source.pickRef(out, 'v1'), { commit: c, branch: null });
  assert.equal(source.pickRef(out, 'missing'), null);
});

test('resolves a fork branch and reads the decomp commit it pins', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sms-game-source-'));
  try {
    const decomp = path.join(dir, 'decomp'), port = path.join(dir, 'port');
    fs.mkdirSync(decomp); git(decomp, ['init', '-q', '-b', 'main']);
    fs.writeFileSync(path.join(decomp, 'a'), '1'); git(decomp, ['add', 'a']); git(decomp, ['commit', '-qm', 'decomp']);
    const pin = git(decomp, ['rev-parse', 'HEAD']);
    fs.mkdirSync(port); git(port, ['init', '-q', '-b', 'feature']);
    git(port, ['update-index', '--add', '--cacheinfo', `160000,${pin},decomp`]); git(port, ['commit', '-qm', 'port']);
    const head = git(port, ['rev-parse', 'HEAD']);
    git(port, ['config', 'uploadpack.allowAnySHA1InWant', 'true']);

    const resolved = await source.resolve({ port: { repository: port, ref: 'feature' }, decomp: null }, { capture, env: process.env });
    assert.deepEqual([resolved.commit, resolved.branch, resolved.decomp, resolved.decompRepository], [head, 'feature', pin, null]);
    assert.equal(resolved.version, `custom ${head.slice(0, 7)}`);

    const other = await source.resolve({ port: null, decomp: { repository: decomp, ref: 'main' } }, { capture, env: process.env });
    assert.deepEqual([other.commit, other.decomp, other.decompRepository], [release.commit, pin, decomp]);
    assert.match(other.version, /\+ decomp /);
    await assert.rejects(source.resolve({ port: { repository: port, ref: 'nope' } }, { capture, env: process.env }), /Could not find "nope"/);
    assert.equal(await source.resolve(null, { capture }), release);
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});
