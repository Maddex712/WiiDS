'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const { execFile, spawnSync } = require('node:child_process');
const { promisify } = require('node:util');
const { activityFromLine } = require('../src/progress');

const python = process.platform === 'win32' ? 'python' : 'python3';
const pythonReady = spawnSync(python, ['--version']).status === 0;

test('texture progress reaches the launcher over pipes while the installer keeps ownership of checks and failures',
  { skip: !pythonReady && 'Python is needed to exercise the portable installer adapter' }, async t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms texture progress '));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const payload = Buffer.alloc(3 * 1024 * 1024, 'test texture archive');
    const checksum = crypto.createHash('sha256').update(payload).digest('hex');
    const server = http.createServer((req, res) => {
      if (req.url !== '/unknown') res.setHeader('Content-Length', payload.length);
      else { res.write(payload.subarray(0, 65536)); res.end(payload.subarray(65536)); return; }
      if (req.url !== '/known') { res.end(payload); return; }
      res.write(payload.subarray(0, 1024 * 1024));
      setTimeout(() => {
        res.write(payload.subarray(1024 * 1024, 2 * 1024 * 1024));
        setTimeout(() => res.end(payload.subarray(2 * 1024 * 1024)), 600);
      }, 600);
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    t.after(() => new Promise(resolve => server.close(resolve)));
    const installer = path.join(root, 'existing installer.py');
    fs.writeFileSync(installer, `import hashlib, os, sys, urllib.request
assert sys.argv[1:] == os.environ.get('TEST_TEXTURE_ARGS', 'textures').split()
with urllib.request.urlopen(os.environ['TEST_TEXTURE_URL']) as response, open('archive.part', 'wb') as target:
    while True:
        chunk = response.read(65536)
        if not chunk:
            break
        target.write(chunk)
with open('archive.part', 'rb') as downloaded:
    checksum = hashlib.sha256(downloaded.read()).hexdigest()
if checksum != os.environ['TEST_TEXTURE_CHECKSUM']:
    sys.exit('error: the download does not match the expected release')
print('Unpacking GMS.7z', flush=True)
with open('installed.flag', 'w') as installed:
    installed.write(checksum)
print('Installed 1 textures in mods/textures/GMS.', flush=True)
`);
    const run = (route, expected = checksum, args = []) => promisify(execFile)(python,
      [path.resolve(__dirname, '..', 'scripts', 'texture-progress.py'), installer, ...args],
      { cwd: root, timeout: 15000, env: { ...process.env, TEST_TEXTURE_ARGS: args.join(' ') || 'textures',
        TEST_TEXTURE_URL: `http://127.0.0.1:${server.address().port}/${route}`, TEST_TEXTURE_CHECKSUM: expected } });

    const known = await run('known');
    const states = known.stdout.split(/\r?\n/).map(activityFromLine).filter(Boolean);
    assert.ok(states.some(state => state.percent > 0 && state.percent < 100), known.stdout);
    assert.ok(states.some(state => state.percent === 100 && state.detail.startsWith('Downloading')));
    assert.deepEqual(states.slice(-3), [
      { detail: 'Checking HD textures', percent: null },
      { detail: 'Installing HD textures', percent: null },
      { detail: 'HD textures installed', percent: 100 }
    ]);
    assert.equal(fs.readFileSync(path.join(root, 'installed.flag'), 'utf8'), checksum);
    const unknown = await run('unknown');
    const downloadStates = unknown.stdout.split(/\r?\n/).map(activityFromLine)
      .filter(state => state?.detail.startsWith('Downloading'));
    assert.ok(downloadStates.length >= 2);
    assert.ok(downloadStates.every(state => state.percent === null));
    fs.rmSync(path.join(root, 'installed.flag'));
    await assert.rejects(run('bad', 'wrong checksum'), error => {
      assert.equal(error.code, 1);
      assert.match(error.stdout, /Checking HD texture download/);
      assert.doesNotMatch(error.stdout, /Unpacking|Installed/);
      assert.match(error.stderr, /does not match the expected release/);
      return true;
    });
    assert.equal(fs.existsSync(path.join(root, 'installed.flag')), false);
    // The texture extras run the same installer with their own arguments.
    const extras = await run('known', checksum, ['extras', '--if-outdated']);
    assert.match(extras.stdout, /Texture download: \d+\/\d+ bytes/);
    assert.equal(fs.readFileSync(path.join(root, 'installed.flag'), 'utf8'), checksum);
  });
