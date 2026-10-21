'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const tools = require('../src/build-tools');
const { run, runMain } = require('./tool-run');

runMain(async () => {
  const file = path.resolve('tool-assets', 'manifest-linux.json');
  const manifest = JSON.parse(fs.readFileSync(file, 'utf8'));
  const asset = manifest.platforms.linux;
  const archive = path.join(path.dirname(file), asset.name);
  if (await tools.hashFile(archive) !== asset.sha256) throw new Error('Prepared archive checksum changed.');
  const temporary = `${archive}.normalized`;
  await run('python3', [path.join(__dirname, 'normalize-linux-tar.py'), archive, temporary], {},
    'Normalize portable tool archive headers');
  execFileSync('tar', ['-tzf', temporary], { stdio: ['ignore', 'ignore', 'inherit'] });
  fs.renameSync(temporary, archive);
  asset.sha256 = await tools.hashFile(archive);
  asset.size = fs.statSync(archive).size;
  fs.writeFileSync(file, `${JSON.stringify(manifest, null, 2)}\n`);
});
