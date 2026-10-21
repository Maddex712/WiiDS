'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { repairGccSpecs } = require('../src/linux-tool-paths');

test('relocated GCC specs link through paths containing spaces, percent signs and backslashes', {
  skip: process.platform !== 'linux'
}, t => {
  const compiler = spawnSync('sh', ['-c', 'command -v gcc'], { encoding: 'utf8' }).stdout.trim();
  if (!compiler) return t.skip('This integration check requires GCC.');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'sms gcc 100% path\\-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const prefix = path.join(root, 'env');
  const directory = path.join(prefix, 'lib', 'gcc', 'probe', '1');
  fs.mkdirSync(directory, { recursive: true });
  const specs = path.join(directory, 'specs');
  fs.writeFileSync(specs, `%rename link original_link\n*link:\n%(original_link) -rpath ${prefix}/lib\n`);
  const source = path.join(root, 'main.c');
  const output = path.join(root, 'probe');
  fs.writeFileSync(source, 'int main(void){return 0;}\n');
  const args = [`-specs=${specs}`, source, '-o', output];
  const broken = spawnSync(compiler, args, { encoding: 'utf8' });
  assert.notEqual(broken.status, 0, 'The unescaped fixture must reproduce the relocation failure.');
  repairGccSpecs(prefix);
  const repaired = fs.readFileSync(specs, 'utf8');
  repairGccSpecs(prefix);
  assert.equal(fs.readFileSync(specs, 'utf8'), repaired, 'Repair must be idempotent.');
  const result = spawnSync(compiler, args, { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  assert.equal(spawnSync(output).status, 0);
});
