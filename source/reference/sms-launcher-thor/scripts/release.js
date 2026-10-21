'use strict';

const { spawnSync } = require('node:child_process');

const repository = process.env.GITHUB_REPOSITORY || '';
if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repository)) {
  console.error('Set GITHUB_REPOSITORY to the launcher release repository (owner/repo).');
  process.exit(1);
}
const [owner, repo] = repository.split('/');
const expectedTag = `v${require('../package.json').version}`;
const requestedTag = process.env.SMS_RELEASE_TAG ||
  (process.env.GITHUB_REF_TYPE === 'tag' ? process.env.GITHUB_REF_NAME : '');
if (requestedTag && requestedTag !== expectedTag) {
  console.error(`Release tag ${requestedTag} does not match package version ${expectedTag}.`);
  process.exit(1);
}
const target = process.platform === 'win32' ? ['--win', '--x64']
  : process.platform === 'darwin' ? ['--mac', '--universal'] : ['--linux', '--x64'];
// A Mac certificate is self-signed, which electron-builder will not pick as an identity.
const macSigning = process.platform === 'darwin' && process.env.CSC_LINK
  ? ['--config.mac.identity=-', '--config.mac.sign=scripts/mac-sign.js'] : [];
const result = spawnSync(process.execPath, [require.resolve('electron-builder/cli.js'), ...target, ...macSigning,
  '--publish', 'always', '--config.publish.provider=github',
  `--config.publish.owner=${owner}`, `--config.publish.repo=${repo}`], { stdio: 'inherit' });
if (result.error) { console.error(result.error); process.exit(1); }
process.exit(result.status ?? 1);
