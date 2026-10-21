'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { toolsetFor, hashFile } = require('../src/build-tools');
const { runMain } = require('./tool-run');

async function main() {
  const directory = path.resolve('tool-assets');
  const mac = process.env.SMS_MAC_TOOL_ASSETS === '1';
  const manifestName = mac ? 'mac-tool-assets.json' : 'tool-assets.json';
  const manifest = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'src', manifestName), 'utf8'));
  // A publisher branch may stage the next tool version while still pointing
  // at an older asset. Unselected platforms must retain their actual version.
  for (const asset of Object.values(manifest.platforms)) {
    const publishedVersion = asset.name.match(/-(\d{4}-\d{2}-\d{2}\.\d+)\.tar\.gz$/)?.[1];
    if (publishedVersion) asset.toolset = publishedVersion;
  }
  const groups = new Map();
  for (const platform of mac ? ['x64', 'arm64'] : ['linux', 'win32']) {
    const file = path.join(directory, `manifest-${mac ? `macos-${platform}` : platform}.json`);
    if (!fs.existsSync(file)) continue;
    const part = JSON.parse(fs.readFileSync(file, 'utf8'));
    const asset = part.platforms[platform];
    const expected = toolsetFor(mac ? 'darwin' : platform, platform);
    if (!asset || (asset.toolset || part.toolset) !== expected) throw new Error(`Unexpected tool version for ${platform}`);
    if (await hashFile(path.join(directory, asset.name)) !== asset.sha256) throw new Error(`Archive checksum changed for ${platform}`);
    asset.toolset = expected;
    manifest.platforms[platform] = asset;
    const tag = new URL(asset.url).pathname.split('/').at(-2);
    const files = [asset.name, new URL(asset.sources).pathname.split('/').at(-1), new URL(asset.notices).pathname.split('/').at(-1)];
    groups.set(tag, [...(groups.get(tag) || []), ...files]);
  }
  if (!groups.size) throw new Error('No tool archives were prepared.');
  fs.writeFileSync(path.join(directory, manifestName), `${JSON.stringify(manifest, null, 2)}\n`);
  for (const [tag, files] of groups) {
    for (const file of files.filter(name => name.endsWith('.tar.gz')))
      execFileSync('tar', ['-tzf', path.join(directory, file)], { stdio: ['ignore', 'ignore', 'inherit'] });
    execFileSync('gh', ['release', 'create', tag, '--repo', 'chasem-dev/sms-launcher', '--target', process.env.GITHUB_SHA,
      '--prerelease', '--title', tag, '--notes',
      'Private build tools verified by compiling the selected game release after relocation. No ROM, game assets, or compiled game is included. Corresponding sources and notices accompany each archive. Each OS tool version is independent and reused across launcher releases. Mac tools still require Apple Command Line Tools and Rosetta on Apple Silicon.',
      ...files.map(file => path.join(directory, file)), path.join(directory, manifestName)], { stdio: 'inherit' });
  }
}

runMain(main);
