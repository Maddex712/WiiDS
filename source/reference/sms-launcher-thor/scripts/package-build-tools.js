'use strict';

// Creates tool-only release assets. Never reads the port checkout or a disc image.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const tar = require('tar');
const YAML = require('yaml');
const { execFileSync } = require('node:child_process');
const tools = require('../src/build-tools');
const { prepareFromUpstream } = require('./bootstrap-build-tools');
const { run, runMain } = require('./tool-run');

const platform = process.platform;
const id = tools.platformId();
const toolset = tools.toolsetFor();
if (tools.assetFor().name.endsWith(`-${toolset}.tar.gz`))
  throw new Error('Bump this platform entry’s toolset before publishing a new immutable archive.');
const output = path.resolve(process.env.SMS_TOOL_ASSET_OUTPUT || 'tool-assets');
const userData = process.env.SMS_TOOLS_PREPARED_DATA || path.join(os.tmpdir(), 'sms-tools-publisher');
const root = tools.rootFor(userData);
const sourceRoot = path.join(output, 'source-work', id);
const notices = ['# Build tool notices and source code', '',
  'These assets contain third-party build tools, not game code or game data.',
  'Each package retains its own license. See the installed license files and the corresponding source asset.',
  'The source asset contains upstream sources, build recipes, patches, and package metadata.',
  `Source download: https://github.com/chasem-dev/sms-launcher/releases/download/build-tools-${id}-${toolset}/sms-build-tools-${id}-${toolset}-sources.tar.gz`, ''];
if (platform === 'darwin') notices.push('Apple Command Line Tools, its SDK/compiler, and Rosetta are not included. The clang/clang++ wrapper scripts invoke the existing Apple compiler installed by the user.', '');
const packages = [];
const downloaded = new Map();
// Use a mirror only when the recipe pins these exact upstream bytes.
const SOURCE_MIRRORS = {
  '053794d6671a3e397d849e478a80b82a63cb9d8ca296bd35b73317bb5ceb87b5':
    ['https://ftp.osuosl.org/pub/gentoo/distfiles/62/pulseaudio-17.0.tar.xz']
};

function flattenLinks(prefix, toolTree = fs.realpathSync(prefix)) {
  if (platform !== 'win32') fs.chmodSync(prefix, fs.statSync(prefix).mode | 0o700);
  for (const entry of fs.readdirSync(prefix, { withFileTypes: true })) {
    const file = path.join(prefix, entry.name);
    if (entry.isDirectory()) flattenLinks(file, toolTree);
    else if (entry.isFile() && platform !== 'win32') fs.chmodSync(file, fs.statSync(file).mode | 0o200);
    else if (entry.isSymbolicLink()) {
      const target = fs.readlinkSync(file);
      let resolved;
      try { resolved = fs.realpathSync(file); }
      catch (error) { if (error.code === 'ENOENT') continue; throw error; }
      const relative = path.relative(toolTree, resolved);
      if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error(`Nonportable tool symlink: ${file} -> ${target}`);
      if (platform === 'win32') {
        // Native Windows symlinks require privileges; materialize these aliases in the shipped tree.
        const copy = `${file}.portable-copy`;
        fs.cpSync(resolved, copy, { recursive: true });
        fs.unlinkSync(file);
        fs.renameSync(copy, file);
        if (fs.statSync(file).isDirectory()) flattenLinks(file, toolTree);
      } else {
        const direct = path.relative(fs.realpathSync(path.dirname(file)), resolved);
        if (target !== direct) { fs.unlinkSync(file); fs.symlinkSync(direct, file); }
      }
    }
  }
}

async function downloadSource(url, name, expected = null) {
  if (!/^https:\/\//.test(url)) throw new Error(`Source URL is not HTTPS: ${url}`);
  const key = expected || url;
  if (downloaded.has(key)) return downloaded.get(key);
  const destination = path.join(sourceRoot, 'upstream', name.replace(/[^a-zA-Z0-9._-]/g, '_'));
  let downloadedUrl = url;
  if (!fs.existsSync(destination) || (expected && await tools.hashFile(destination) !== expected)) {
    const candidates = [url, ...(SOURCE_MIRRORS[expected] || [])];
    for (let index = 0; index < candidates.length; index++) {
      try {
        await run(platform === 'win32' ? path.join(root, 'msys64', 'usr', 'bin', 'curl.exe')
          : path.join(root, 'env', 'bin', 'curl'), ['--fail', '--location', '--retry', '5', '--retry-all-errors',
          '--retry-delay', '5', '--connect-timeout', '30', '--max-time', '600', '--silent', '--show-error',
          '--output', `${destination}.part`, candidates[index]], {}, `Source: ${name}`);
        downloadedUrl = candidates[index];
        break;
      } catch (error) {
        if (index === candidates.length - 1) throw error;
      }
    }
    fs.renameSync(`${destination}.part`, destination);
  }
  const sha256 = await tools.hashFile(destination);
  if (expected && sha256 !== expected) throw new Error(`Source checksum failed: ${url}`);
  const item = { url: downloadedUrl, file: `upstream/${path.basename(destination)}`, sha256 };
  downloaded.set(key, item);
  return item;
}

async function condaSources() {
  const metaDir = path.join(root, 'env', 'conda-meta');
  for (const file of fs.readdirSync(metaDir).filter(name => name.endsWith('.json')).sort()) {
    const meta = JSON.parse(fs.readFileSync(path.join(metaDir, file), 'utf8'));
    if (platform === 'darwin' && /(?:^|[-_])(?:sdk|sysroot)(?:$|[-_])/.test(meta.name))
      throw new Error(`Apple SDKs must not enter the Mac archive: ${meta.name}`);
    const info = path.join(meta.link.source, 'info');
    if (fs.existsSync(path.join(info, 'licenses')))
      fs.cpSync(path.join(info, 'licenses'), path.join(root, 'env', 'share', 'sms-launcher-licenses', `${meta.name}-${meta.version}`), { recursive: true });
    const destination = path.join(sourceRoot, 'recipes', `${meta.name}-${meta.version}-${meta.build}`);
    fs.cpSync(info, destination, { recursive: true, filter: name => !name.endsWith('/paths.json') });
    const recipeFile = ['recipe/meta.yaml', 'recipe/rendered_recipe.yaml'].map(name => path.join(info, name)).find(fs.existsSync);
    const recipe = recipeFile ? YAML.parse(fs.readFileSync(recipeFile, 'utf8')) : {};
    const sources = [];
    for (const source of [].concat(recipe.source || [])) {
      const urls = [].concat(source.url || []);
      if (urls.length) {
        const url = (urls.find(value => /^https?:\/\//.test(value)) || '').replace(/^http:/, 'https:');
        if (!url) throw new Error(`No HTTPS source for ${meta.name}`);
        if (url.endsWith('.rpm')) {
          // Sysroot recipes repack binary RPMs; distribute their source RPMs instead.
          const rpm = url.match(/\/rocky\/([^/]+)\/.*\/(glibc|kernel)(?:-[a-z-]+)?-(\d[^/]+)\.x86_64\.rpm$/);
          const centos = url.match(/vault\.centos\.org\/(?:centos\/)?([^/]+)\/(os|updates)\/.*\/(glibc|kernel|nss-softokn)(?:-[a-z-]+)?-(\d[^/]+)\.x86_64\.rpm$/);
          if (!rpm && !centos) throw new Error(`Add a corresponding source RPM for ${url}`);
          const filename = rpm ? `${rpm[2]}-${rpm[3]}.src.rpm` : `${centos[3]}-${centos[4]}.src.rpm`;
          const sourceUrl = rpm ? `https://download.rockylinux.org/vault/rocky/${rpm[1]}/BaseOS/source/tree/Packages/${rpm[2][0]}/${filename}`
            : `https://vault.centos.org/${centos[1]}/${centos[2]}/Source/SPackages/${filename}`;
          sources.push(await downloadSource(sourceUrl, filename));
        } else sources.push(await downloadSource(url, `${meta.name}-${path.basename(new URL(url).pathname)}`, source.sha256 || null));
      } else if (source.git_url) {
        throw new Error(`Add a source snapshot for ${meta.name}: ${source.git_url} ${source.git_rev || ''}`);
      }
    }
    const item = { name: meta.name, version: meta.version, build: meta.build, license: meta.license,
      binary: meta.url, sources, recipe: `recipes/${path.basename(destination)}` };
    packages.push(item);
    notices.push(`- ${meta.name} ${meta.version} (${meta.license || 'see package source'})`);
  }
}

async function linux32Sources() {
  const { SDK } = require('./prepare-linux32');
  const sdk = path.join(root, 'env', 'targets', 'linux32');
  const recipe = path.join(sourceRoot, 'recipes', 'linux32-sdk');
  fs.mkdirSync(recipe, { recursive: true });
  for (const name of ['README.txt', 'summary.csv', 'buildroot.config'])
    fs.copyFileSync(path.join(sdk, name), path.join(recipe, name));
  const buildroot = await downloadSource(SDK.buildroot, 'bootlin-buildroot-toolchains-2025.08.1.tar.gz');
  const rows = JSON.parse(execFileSync(path.join(root, 'env', 'bin', 'python3'), ['-c',
    'import csv,json,sys; print(json.dumps(list(csv.reader(open(sys.argv[1])))[1:]))', path.join(sdk, 'summary.csv')], { encoding: 'utf8' }));
  const seen = new Set();
  for (const [name, version, license, licenseFiles, archive] of rows) {
    const key = `${name}-${version}`;
    const item = { name, version, license, recipe: 'recipes/linux32-sdk', sources: [buildroot] };
    if (archive !== 'not saved' && !seen.has(archive)) {
      item.sources.push(await downloadSource(`https://toolchains.bootlin.com/downloads/releases/sources/${key}/${archive}`, archive));
      seen.add(archive);
    }
    for (const file of licenseFiles.split(' ').filter(Boolean)) {
      const licenseUrl = name === 'buildroot'
        ? `https://raw.githubusercontent.com/bootlin/buildroot-toolchains/toolchains.bootlin.com-2025.08.1/${file}`
        : `https://toolchains.bootlin.com/downloads/releases/licenses/${key}/${file}`;
      const source = await downloadSource(licenseUrl,
        `${key}-${file.replaceAll('/', '-')}`);
      const destination = path.join(sdk, 'share', 'sms-launcher-licenses', key, file);
      fs.mkdirSync(path.dirname(destination), { recursive: true });
      fs.copyFileSync(path.join(sourceRoot, source.file), destination);
    }
    packages.push(item);
    notices.push(`- Linux 32-bit SDK: ${name} ${version} (${license})`);
  }
  const graphics = path.join(sdk, 'graphics');
  const debSources = path.join(graphics, 'package-sources');
  fs.cpSync(debSources, path.join(sourceRoot, 'debian-sources'), { recursive: true });
  for (const line of fs.readFileSync(path.join(graphics, 'packages.tsv'), 'utf8').trim().split('\n')) {
    const [name, version, source, sourceVersion] = line.split('\t');
    const copyright = path.join(graphics, 'usr', 'share', 'doc', name.replace(/:i386$/, ''), 'copyright');
    packages.push({ name, version, source, sourceVersion, recipe: 'recipes/linux32-sdk/prepare-linux32-graphics.sh',
      files: await Promise.all(fs.readdirSync(path.join(debSources, source)).map(async file => ({
        file: `debian-sources/${source}/${file}`, sha256: await tools.hashFile(path.join(debSources, source, file)) }))) });
    notices.push(`- Linux 32-bit graphics: ${name} ${version} (see usr/share/doc/${name.replace(/:i386$/, '')}/copyright)`);
    if (!fs.existsSync(copyright)) throw new Error(`Missing Debian license information for ${name}`);
  }
  fs.copyFileSync(path.join(__dirname, 'prepare-linux32-graphics.sh'), path.join(recipe, 'prepare-linux32-graphics.sh'));
  fs.copyFileSync(path.join(__dirname, 'prepare-linux32.js'), path.join(recipe, 'prepare-linux32.js'));
  fs.copyFileSync(path.join(graphics, 'packages.tsv'), path.join(recipe, 'debian-packages.tsv'));
  fs.rmSync(debSources, { recursive: true, force: true });
}

function dbField(text, name) { return text.match(new RegExp(`%${name}%\\r?\\n([\\s\\S]*?)(?:\\r?\\n\\r?\\n|$)`))?.[1].trim() || ''; }

async function windowsSources() {
  const db = path.join(root, 'msys64', 'var', 'lib', 'pacman', 'local');
  const sourceNames = new Set();
  for (const directory of fs.readdirSync(db).sort()) {
    const desc = path.join(db, directory, 'desc');
    if (!fs.existsSync(desc)) continue;
    const contents = fs.readFileSync(desc, 'utf8');
    const name = dbField(contents, 'NAME'), version = dbField(contents, 'VERSION');
    const base = dbField(contents, 'BASE') || name;
    const sourceName = `${base}-${version}.src.tar.zst`;
    const sourceUrl = `https://repo.msys2.org/${base.startsWith('mingw-w64-') && !base.startsWith('mingw-w64-cross-') ? 'mingw' : 'msys'}/sources/${sourceName}`;
    let source;
    if (!sourceNames.has(sourceName)) {
      source = await downloadSource(sourceUrl, sourceName);
      sourceNames.add(sourceName);
    } else source = downloaded.get(sourceUrl);
    packages.push({ name, version, license: dbField(contents, 'LICENSE').split(/\r?\n/), source });
    notices.push(`- ${name} ${version} (${dbField(contents, 'LICENSE').replace(/\r?\n/g, ', ')})`);
  }
  fs.cpSync(db, path.join(sourceRoot, 'package-metadata'), { recursive: true });
}

async function main() {
  if (!['linux', 'win32', 'darwin'].includes(platform)) throw new Error('Unsupported tool asset platform.');
  fs.mkdirSync(output, { recursive: true });
  fs.mkdirSync(path.join(sourceRoot, 'upstream'), { recursive: true });
  if (!process.env.SMS_TOOLS_PREPARED_DATA) await prepareFromUpstream(userData, { forcePrivate: true, run });
  const name = `sms-build-tools-${id}-${toolset}.tar.gz`;
  const archive = path.join(output, name);
  const noticesFile = `sms-build-tools-${id}-${toolset}-NOTICES.md`;
  if (platform !== 'win32') {
    await condaSources();
    if (platform === 'linux') await linux32Sources();
    const prefix = path.join(root, 'env');
    fs.writeFileSync(path.join(prefix, 'THIRD-PARTY-NOTICES.md'), `${notices.join('\n')}\n`);
    flattenLinks(prefix);
    const helper = path.join(output, 'conda-pack-helper');
    const env = { ...process.env, PATH: [path.join(prefix, 'bin'), process.env.PATH].join(path.delimiter), PYTHONPATH: helper };
    await run(path.join(prefix, 'bin', 'python3'), ['-m', 'pip', 'install', '--target', helper, 'conda-pack==0.9.1'], { env }, 'Prepare archive packer');
    await run(path.join(prefix, 'bin', 'python3'), [path.join(__dirname, 'pack-conda-env.py'),
      prefix, archive], { env }, 'Pack relocatable build tools');
  } else {
    await windowsSources();
    fs.writeFileSync(path.join(root, 'msys64', 'THIRD-PARTY-NOTICES.md'), `${notices.join('\n')}\n`);
    flattenLinks(path.join(root, 'msys64'));
    // Preserve the prepared tree, but exclude package downloads, caches, and machine-specific homes.
    process.stdout.write('\nPack Windows build tools\n');
    tar.c({ sync: true, gzip: true, file: archive, cwd: root, portable: true,
      filter: name => !/^msys64\/(?:var\/cache|home|tmp)(?:\/|$)/.test(name.replaceAll('\\', '/')) }, ['msys64']);
  }
  fs.writeFileSync(path.join(output, noticesFile), `${notices.join('\n')}\n`);
  fs.writeFileSync(path.join(sourceRoot, 'packages.json'), `${JSON.stringify(packages, null, 2)}\n`);
  fs.copyFileSync(path.join(output, noticesFile), path.join(sourceRoot, 'THIRD-PARTY-NOTICES.md'));
  const sourcesName = `sms-build-tools-${id}-${toolset}-sources.tar.gz`;
  process.stdout.write('\nPack corresponding source files\n');
  tar.c({ sync: true, gzip: true, file: path.join(output, sourcesName), cwd: sourceRoot, portable: true }, fs.readdirSync(sourceRoot));
  for (const file of [archive, path.join(output, sourcesName)])
    if (fs.statSync(file).size >= 2 ** 31) throw new Error(`Release asset exceeds GitHub's size limit: ${file}`);
  const tag = `build-tools-${id}-${toolset}`;
  const base = `https://github.com/chasem-dev/sms-launcher/releases/download/${tag}`;
  const manifest = { toolset, platforms: { [platform === 'darwin' ? process.arch : platform]: { toolset, name, url: `${base}/${name}`,
    sha256: await tools.hashFile(archive), size: fs.statSync(archive).size, sources: `${base}/${sourcesName}`,
    notices: `${base}/${noticesFile}` } } };
  fs.writeFileSync(path.join(output, `manifest-${platform === 'darwin' ? id : platform}.json`), `${JSON.stringify(manifest, null, 2)}\n`);
  fs.rmSync(path.join(output, 'source-work'), { recursive: true, force: true });
  fs.rmSync(path.join(output, 'conda-pack-helper'), { recursive: true, force: true });
  process.stdout.write(`Tool asset ready: ${archive}\n`);
}

runMain(main);
