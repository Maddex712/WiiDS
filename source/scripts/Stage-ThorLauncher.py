"""Prepare optional private case/disc artwork verbatim; stage only with --stage."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FOLDER = ROOT / 'build/thor-map-data'
ASSETS = FOLDER / 'launcher'
STEMS = ('original-cover', 'retro-rewind-cover', 'original-disc', 'retro-rewind-disc')
# Later library games are optional, but a manifest carries both of a game's images or neither.
OPTIONAL = (('nsmbw-cover', 'nsmbw-disc'), ('sunshine-cover', 'sunshine-disc'), ('msmbwii-cover', 'msmbwii-disc'),
            ('eclipse-cover', 'eclipse-disc'))
SPINES = tuple(game + '-spine' for game in ('original', 'retro-rewind', 'nsmbw', 'sunshine', 'msmbwii', 'eclipse'))
# Everything else is an owner-provided reference (the NSMB disc scan is squared to 1024 px).
SOURCES = {'retro-rewind-disc': 'generated-fan-disc', 'msmbwii-cover': 'composed-from-owner-nsmbw-art',
           'msmbwii-disc': 'composed-from-owner-nsmbw-art'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(path, spine=False):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 24 * 1024 * 1024:
        raise ValueError(f'Invalid or oversized launcher image: {path.name}')
    with Image.open(path) as image:
        kind, size = image.format, image.size
        if kind not in ('PNG', 'JPEG') or not ((16 if spine else 128) <= size[0] <= 4096 and 128 <= size[1] <= 4096):
            raise ValueError(f'Invalid launcher image dimensions or format: {path.name}')
        image.verify()
    return kind, list(size)


def validate():
    manifest = ASSETS / 'artwork.json'
    if not manifest.exists():
        return []  # The native-art launcher remains available without optional covers.
    if ASSETS.is_symlink() or manifest.is_symlink() or not manifest.is_file():
        raise ValueError('Launcher artwork directory/manifest must be regular')
    data = json.loads(manifest.read_text(encoding='utf8'))
    keys = set(data.get('images', {}))
    optional = tuple(stem for pair in OPTIONAL for stem in pair)
    if (data.get('schema') != 1 or data.get('kind') != 'private-launcher-artwork' or not set(STEMS) <= keys
            or not keys <= set(STEMS + optional + SPINES) or any(keys & set(pair) and not set(pair) <= keys for pair in OPTIONAL)
            or any(s in keys and not {s.removesuffix('-spine') + '-cover', s.removesuffix('-spine') + '-disc'} <= keys for s in SPINES)):
        raise ValueError('Unexpected launcher artwork manifest')
    files = [manifest]
    for stem in [s for s in STEMS + optional + SPINES if s in keys]:
        entry = data['images'][stem]
        name = entry['file']
        if name not in (stem + '.png', stem + '.jpg'):
            raise ValueError('Unsafe launcher artwork filename')
        path = ASSETS / name
        kind, size = inspect(path, spine=stem in SPINES)
        if path.resolve().parent != ASSETS.resolve() or sha(path) != entry['sha256'] or size != entry['size']:
            raise ValueError(f'Launcher artwork identity mismatch: {name}')
        if (kind == 'PNG') != name.endswith('.png'):
            raise ValueError(f'Launcher artwork extension mismatch: {name}')
        files.append(path)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for stem in STEMS + tuple(stem for pair in OPTIONAL for stem in pair) + SPINES:
        parser.add_argument('--' + stem, type=Path)
    parser.add_argument('--stage', action='store_true')
    parser.add_argument('--serial')
    parser.add_argument('--package', choices=('com.driftdroid.android.thor', 'io.github.maddex712.wiids'),
                        default='com.driftdroid.android.thor', help='Library app receiving private artwork')
    args = parser.parse_args()
    provided = [getattr(args, stem.replace('-', '_')) for stem in STEMS]
    if any(provided) and not all(provided):
        parser.error('Provide all four artwork paths to prepare a set')
    extras = {}
    for pair in OPTIONAL:
        given = {stem: getattr(args, stem.replace('-', '_')) for stem in pair}
        if any(given.values()) and not all(given.values()):
            parser.error('Provide both images of an optional game: ' + ', '.join(pair))
        if all(given.values()):
            extras.update(given)
    extras.update({stem: getattr(args, stem.replace('-', '_')) for stem in SPINES
                   if getattr(args, stem.replace('-', '_')) is not None})
    if args.stage and (not args.serial or any(c.isspace() for c in args.serial)):
        parser.error('--stage requires a nonempty --serial without whitespace')
    if any(provided):
        if ASSETS.is_symlink():
            raise ValueError('Launcher artwork directory cannot be a symlink')
        records = []
        for stem, source in zip(STEMS, provided):
            kind, size = inspect(source)
            records.append((stem, source, kind, size))
        ASSETS.mkdir(parents=True, exist_ok=True)
        data = {'schema': 1, 'kind': 'private-launcher-artwork', 'images': {}}
        for stem, source, kind, size in records:
            target = ASSETS / (stem + ('.png' if kind == 'PNG' else '.jpg'))
            if target.is_symlink():
                raise ValueError('Launcher artwork target cannot be a symlink')
            if source.resolve() != target.resolve():
                shutil.copyfile(source, target)
            data['images'][stem] = {'file': target.name, 'sha256': sha(target), 'size': size,
                                   'source': SOURCES.get(stem, 'owner-provided-reference')}
        (ASSETS / 'artwork.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf8')
    if extras:
        # Add a later game to the existing, already validated set without touching its images.
        if not validate():
            parser.error('Prepare the four base images before adding another game')
        data = json.loads((ASSETS / 'artwork.json').read_text(encoding='utf8'))
        prospective = set(data['images']) | set(extras)
        if any(s in prospective and not {s.removesuffix('-spine') + '-cover', s.removesuffix('-spine') + '-disc'} <= prospective
               for s in SPINES):
            parser.error('A spine requires that game\'s cover and disc images')
        records = [(stem, source) + inspect(source, spine=stem in SPINES) for stem, source in extras.items()]
        for stem, source, kind, size in records:
            target = ASSETS / (stem + ('.png' if kind == 'PNG' else '.jpg'))
            if target.is_symlink():
                raise ValueError('Launcher artwork target cannot be a symlink')
            if source.resolve() != target.resolve():
                shutil.copyfile(source, target)
            data['images'][stem] = {'file': target.name, 'sha256': sha(target), 'size': size,
                                   'source': SOURCES.get(stem, 'owner-provided-reference')}
        (ASSETS / 'artwork.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf8')
    files = validate()
    print(f'Validated {len(files)} optional private launcher files')
    if not args.stage:
        print('No device contacted; --stage is explicit')
        return
    if not files:
        parser.error('Prepare artwork before staging')
    target = 'files/WiiCompiled/ThorMap'
    archive = ROOT / 'build/thor-launcher-art.tar'
    with tarfile.open(archive, 'w') as tar:
        for path in files:
            tar.add(path, arcname=path.relative_to(FOLDER).as_posix(), recursive=False)
        checks = ''.join(f'{sha(p)}  {p.relative_to(FOLDER).as_posix()}\n' for p in files).encode()
        entry = tarfile.TarInfo('.thor-launcher.sha256'); entry.size = len(checks)
        tar.addfile(entry, io.BytesIO(checks))
    adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
    subprocess.run(adb + ['shell', 'run-as', args.package, 'mkdir', '-p', target + '/launcher'], check=True)
    with archive.open('rb') as stream:
        subprocess.run(adb + ['exec-in', 'run-as', args.package, 'tar', '-xf', '-', '-C', target],
                       stdin=stream, check=True, timeout=120)
    subprocess.run(adb + ['shell', 'run-as', args.package, 'sh', '-c',
                         f"'cd {target} && sha256sum -cs .thor-launcher.sha256'"], check=True, timeout=120)
    receipt = {'status': 'device_hashes_match', 'package': args.package, 'file_count': len(files),
               'files': {p.relative_to(FOLDER).as_posix(): sha(p) for p in files}}
    (ROOT / 'build/thor-launcher-device-staging.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Staged launcher artwork; all device hashes match')


if __name__ == '__main__':
    main()
