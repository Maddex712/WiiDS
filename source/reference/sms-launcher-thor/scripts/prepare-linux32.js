'use strict';

// All compiler/helper executables are x86_64. The sysroot and graphics files
// describe the 32-bit game target and stay inside the private tool archive.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const tools = require('../src/build-tools');

const SDK = {
  name: 'x86-i686--glibc--stable-2025.08-1',
  url: 'https://toolchains.bootlin.com/downloads/releases/toolchains/x86-i686/tarballs/x86-i686--glibc--stable-2025.08-1.tar.xz',
  sha256: 'f9b365d7fd8f8860dfb1845ff731013550f071ca4a8de0e5074abc995296b26c',
  buildroot: 'https://github.com/bootlin/buildroot-toolchains/archive/refs/tags/toolchains.bootlin.com-2025.08.1.tar.gz'
};

async function prepare(prefix, run) {
  const sdk = path.join(prefix, 'targets', 'linux32');
  fs.mkdirSync(sdk, { recursive: true });
  const archive = path.join(path.dirname(prefix), 'linux32-sdk.tar.xz');
  if (!fs.existsSync(archive) || await tools.hashFile(archive) !== SDK.sha256)
    await tools.fetchVerified(SDK.url, archive, SDK.sha256);
  await run('tar', ['-xJf', archive, '--strip-components=1', '-C', sdk], {}, 'Prepare x64-host Linux 32-bit compiler');
  const graphics = path.join(sdk, 'graphics');
  fs.mkdirSync(graphics, { recursive: true });
  const { uid, gid } = os.userInfo();
  await run('docker', ['run', '--rm', '--platform', 'linux/amd64',
    '--env', `SMS_PUBLISH_UID=${uid}`, '--env', `SMS_PUBLISH_GID=${gid}`,
    '--mount', `type=bind,source=${graphics},target=/out`,
    '--mount', `type=bind,source=${path.join(__dirname, 'prepare-linux32-graphics.sh')},target=/prepare.sh,readonly`,
    'debian:bookworm-slim', '/bin/bash', '/prepare.sh'], {}, 'Prepare private 32-bit graphics and corresponding sources');
}

module.exports = { SDK, prepare };
