'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const tools = require('../src/build-tools');
const { runMain } = require('./tool-run');

async function main() {
  const directory = path.resolve('tool-assets');
  fs.mkdirSync(directory, { recursive: true });
  for (const arch of process.platform === 'darwin' ? ['x64', 'arm64'] : [process.arch]) {
    const asset = tools.assetFor(process.platform, arch);
    const archive = path.join(directory, asset.name);
    await tools.fetchVerified(asset.url, archive, asset.sha256);
    if (process.env.SMS_RELEASE_TAG)
      execFileSync('gh', ['release', 'upload', process.env.SMS_RELEASE_TAG, archive,
        '--repo', process.env.GITHUB_REPOSITORY || 'chasem-dev/sms-launcher'], { stdio: 'inherit' });
  }
}

runMain(main);
