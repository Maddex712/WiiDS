"""Stage the owner's approved current presentation, never game archives or profiles.

Reads only the named launcher/audio/HUD folders on the device. Every selected device file is
SHA-256 checked before and after capture. The internal Mario Kart menu/HUD/maps/settings set
comes from its existing host output. Nothing is written to the Thor.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build/presentation-staging'
APP = '/sdcard/Android/data/io.github.maddex712.wiids/files'
GROUPS = {
    'launcher': ([APP + '/custom/launcher'], ROOT / 'build/thor-map-data/launcher', 'artwork.json'),
    'launcher-audio': ([APP + '/custom/launcher-audio'], ROOT / 'build/thor-map-data/launcher-audio', 'audio.json'),
    'nsmbw-hud': ([APP + '/nsmbw/ThorHud'], ROOT / 'build/nsmbw-thor/e1/handheld-assets', 'manifest.json'),
    'msmbwii-hud': ([APP + '/msmbwii/ThorHud'], ROOT / 'build/nsmbw-thor/e1/handheld-assets', 'manifest.json'),
    'sms-hud': ([APP + '/sms/ThorHud'], ROOT / 'build/sunshine-thor/thor-hud', 'manifest.json'),
    'eclipse-hud': ([APP + '/eclipse/ThorHud', '/sdcard/Android/data/com.wither.sms.eclipse.thor/files/ThorHud'],
                    ROOT / 'build/sunshine-thor/thor-hud', 'manifest.json'),
}
NAME = re.compile(r'[A-Za-z0-9_.-]+\Z')
ART_EXT = {'.png', '.jpg', '.wav', '.json'}


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def selected(manifest, marker):
    """Select output references, not the source archives described beside them."""
    if marker == 'artwork.json':
        profiles = ('original', 'retro-rewind', 'nsmbw', 'sunshine', 'msmbwii', 'eclipse', 'galaxy', 'kaizo')
        required = {profile + '-' + part for profile in profiles for part in ('cover', 'disc', 'spine')}
        missing = sorted(required - manifest.get('images', {}).keys())
        if missing:
            raise ValueError('Incomplete shipped launcher artwork: ' + ', '.join(missing))
    names = {marker}
    def add(value):
        if isinstance(value, str) and Path(value).suffix.lower() in ART_EXT - {'.json'}:
            if not NAME.fullmatch(value):
                raise ValueError('Unsafe presentation output reference: ' + value)
            names.add(value)
    def walk(value):
        if isinstance(value, dict):
            for key, child in value.items():
                # Sunshine HUD output PNGs are the manifest keys.
                add(key)
                if key in {'file', 'texture', 'png'}:
                    add(child)
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)
    walk(manifest)
    return sorted(names)


def adb_call(adb, serial, *args, check=True):
    result = subprocess.run([str(adb), '-s', serial, *map(str, args)], capture_output=True,
                            text=True, encoding='utf-8', errors='replace', timeout=120)
    if check and result.returncode:
        raise RuntimeError('Read-only ADB operation failed: ' + result.stderr.strip() + result.stdout.strip())
    return result


def remote_hashes(adb, serial, remote, names):
    result = adb_call(adb, serial, 'shell', 'sha256sum', *[remote + '/' + n for n in names])
    hashes = {}
    for line in result.stdout.splitlines():
        digest, _, filename = line.partition('  ')
        if not re.fullmatch('[0-9a-f]{64}', digest) or not filename.startswith(remote + '/'):
            raise ValueError('Unexpected device SHA-256 output')
        name = filename[len(remote) + 1:]
        if name not in names:
            raise ValueError('Unexpected file in device hash output')
        hashes[name] = digest
    if set(hashes) != set(names):
        raise ValueError('Missing device presentation hashes')
    return hashes


def capture(adb, serial, group, remotes, marker, folder):
    remote = None
    for candidate in remotes:
        found = adb_call(adb, serial, 'shell', 'test', '-f', candidate + '/' + marker, check=False)
        if found.returncode == 0:
            remote = candidate
            break
    if remote is None:
        return None
    folder.mkdir(parents=True, exist_ok=True)
    adb_call(adb, serial, 'pull', remote + '/' + marker, folder / marker)
    names = selected(json.loads((folder / marker).read_text(encoding='utf-8')), marker)
    before = remote_hashes(adb, serial, remote, names)
    def pull(name):
        target = folder / name
        if not target.is_file() or sha(target) != before[name]:
            adb_call(adb, serial, 'pull', remote + '/' + name, target)
        if sha(target) != before[name]:
            raise ValueError('Captured presentation hash mismatch: ' + group + '/' + name)
    with ThreadPoolExecutor(max_workers=4) as workers:
        list(workers.map(pull, names))
    if remote_hashes(adb, serial, remote, names) != before:
        raise ValueError('Presentation changed while being captured; retry the read-only capture')
    if selected(json.loads((folder / marker).read_text(encoding='utf-8')), marker) != names:
        raise ValueError('Presentation manifest changed its file table during capture')
    return {'source': 'device', 'remote': remote, 'files': names, 'sha256': before}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial')
    parser.add_argument('--adb', type=Path, default=ROOT / '.tools/platform-tools/adb.exe')
    parser.add_argument('--capture-root', type=Path, default=BUILD / 'device-capture')
    parser.add_argument('--launcher-root', type=Path, help='Use a prepared launcher artwork set instead of a device/captured set')
    parser.add_argument('--thor-map', type=Path, default=ROOT / 'build/thor-map-data')
    parser.add_argument('--retro-maps', type=Path, default=ROOT / 'build/thor-rr-map-data')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/wiids-merge/assets/presentation')
    parser.add_argument('--revision', default='thor-2026-10-20-r2')
    args = parser.parse_args()
    if args.serial and not re.fullmatch(r'[A-Za-z0-9_.:-]+', args.serial):
        parser.error('Unsafe ADB serial')
    if not NAME.fullmatch(args.revision):
        parser.error('Unsafe presentation revision')
    # The only directory replaced by this helper is its named ignored presentation output.
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / 'build').resolve()) or output.name != 'presentation':
        parser.error('Output must be a presentation directory inside this workspace build folder')
    BUILD.mkdir(parents=True, exist_ok=True)
    staged = BUILD / ('payload-' + uuid.uuid4().hex)
    staged.mkdir()
    receipt = {'schema': 1, 'created_utc': datetime.now(timezone.utc).isoformat(),
               'device_writes': False, 'groups': {}, 'roms_profiles_logs_archives_included': False}
    entries = []
    def copy(group, source, relative, replacement=None):
        if source.is_symlink():
            raise ValueError('Symbolic-link presentation input')
        source = source.resolve(strict=True)
        if source.is_symlink() or not source.is_file():
            raise ValueError('Nonregular presentation input')
        target = group + '/' + relative
        path = PurePosixPath(target)
        if '..' in path.parts or any(not NAME.fullmatch(p) for p in path.parts):
            raise ValueError('Unsafe presentation target')
        size = len(replacement) if replacement is not None else source.stat().st_size
        if not 0 < size <= 128 * 1024 * 1024:
            raise ValueError('Presentation file exceeds supported bounds: ' + target)
        allowed = {'.rgba', '.tsv'} if relative.startswith('thor_settings/') else ART_EXT
        if source.suffix.lower() not in allowed:
            raise ValueError('Unexpected non-presentation input: ' + target)
        destination = staged / 'files' / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        if replacement is None:
            shutil.copyfile(source, destination)
            digest = sha(source)
        else:
            destination.write_bytes(replacement)
            digest = hashlib.sha256(replacement).hexdigest()
        if sha(destination) != digest:
            raise ValueError('Presentation copy mismatch')
        entries.append({'asset': 'presentation/files/' + target, 'target': target, 'sha256': digest, 'bytes': size})
    for group, (remotes, fallback, marker) in GROUPS.items():
        capture_relative = {'launcher': 'custom/launcher', 'launcher-audio': 'custom/launcher-audio',
                            'nsmbw-hud': 'nsmbw/ThorHud', 'msmbwii-hud': 'msmbwii/ThorHud',
                            'sms-hud': 'sms/ThorHud', 'eclipse-hud': 'eclipse/ThorHud'}
        folder = args.capture_root / (capture_relative[group] if (args.capture_root / 'custom').is_dir() else group)
        captured = capture(args.adb, args.serial, group, remotes, marker, folder) if args.serial and not (group == 'launcher' and args.launcher_root) else None
        if group == 'launcher' and args.launcher_root:
            folder = args.launcher_root.resolve(strict=True)
            choice = {'source': 'explicit-prepared-launcher', 'files': selected(json.loads((folder / marker).read_text(encoding='utf-8')), marker)}
        elif captured:
            choice = captured
        elif (folder / marker).is_file():
            choice = {'source': 'existing-capture', 'files': selected(json.loads((folder / marker).read_text(encoding='utf-8')), marker)}
        else:
            folder = fallback
            choice = {'source': 'host-fallback', 'host': str(folder), 'files': selected(json.loads((folder / marker).read_text(encoding='utf-8')), marker)}
        receipt['groups'][group] = choice
        for name in choice['files']:
            copy(group, folder / name, name)
    maps = args.thor_map.resolve(strict=True)
    map_names = []
    for path in sorted(maps.iterdir()):
        if path.is_file() and path.suffix.lower() in {'.png', '.json'}:
            if not re.fullmatch(r'(?:course_[0-3][0-9]|face_[0-9]{2}|hud(?:_[A-Za-z0-9_]+)?|item_[0-9]{2}|menu(?:_[A-Za-z0-9_]+)?|faces|items)\.(?:png|json)', path.name):
                raise ValueError('Unrecognized host ThorMap output: ' + path.name)
            copy('thor-map', path, path.name)
            map_names.append(path.name)
    for path in sorted((maps / 'thor_settings').iterdir()):
        if path.is_file() and (path.name in {'settings.tsv', 'font.rgba', 'background.rgba', 'panel.rgba', 'header.rgba', 'footer.rgba', 'button.rgba', 'button_focus.rgba'}):
            copy('thor-map', path, 'thor_settings/' + path.name)
            map_names.append('thor_settings/' + path.name)
    required_maps = {f'course_{i:02}.{ext}' for i in range(32) for ext in ('png', 'json')}
    if not required_maps.issubset(map_names) or not {'hud.json', 'menu.json', 'thor_settings/settings.tsv'}.issubset(map_names):
        raise ValueError('Host ThorMap presentation is incomplete')
    receipt['groups']['thor-map'] = {'source': 'host-existing-output', 'host': str(maps), 'files': map_names}
    # The exact cached visual maps are presentation PNGs, not the mod's game archives.
    # Preserve every source/archive hash used by the runtime's loaded-course identity gate.
    retro = args.retro_maps.resolve(strict=True)
    retro_manifest = json.loads((retro / 'maps.json').read_text(encoding='utf-8'))
    if retro_manifest.get('schema') != 1 or retro_manifest.get('profile') != 'retro_rewind':
        raise ValueError('Unsupported Retro Rewind map manifest')
    changes, retro_files = [], []
    def scrub(value, location=''):
        if isinstance(value, dict):
            out = {}
            for key, child in value.items():
                here = (location + '.' if location else '') + key
                if isinstance(child, str) and re.search(r'[A-Za-z]:[\\/]', child):
                    changes.append(here)
                    continue
                out[key] = scrub(child, here)
            return out
        if isinstance(value, list):
            return [scrub(child, location + '[]') for child in value]
        return value
    for key, record in retro_manifest.get('courses', {}).items():
        if not re.fullmatch(r'[A-Za-z0-9_-]+', key) or record.get('key') != key or record.get('archive_name') != key + '.szs':
            raise ValueError('Unsafe or inconsistent cached map identity')
        png, record_name = record.get('png'), record.get('receipt')
        if png != f'course_{key}.png' or record_name != f'course_{key}.json':
            raise ValueError('Unexpected cached map filenames')
        source_hash = record.get('source_sha256', '')
        if not re.fullmatch(r'[0-9a-f]{64}', source_hash):
            raise ValueError('Missing course archive hash gate')
        detail = json.loads((retro / record_name).read_text(encoding='utf-8'))
        if detail.get('source_sha256') != source_hash or detail.get('archive_name') != record['archive_name']:
            raise ValueError('Cached map manifest/receipt source mismatch')
        if sha(retro / png) != record.get('png_sha256') or detail.get('png_sha256') != record.get('png_sha256'):
            raise ValueError('Cached map PNG hash mismatch: ' + key)
        copy('thor-map', retro / png, 'retro_rewind/' + png)
        copy('thor-map', retro / record_name, 'retro_rewind/' + record_name,
             (json.dumps(scrub(detail, key), indent=2) + '\n').encode('utf-8'))
        retro_files.extend([png, record_name])
    safe_manifest = scrub(retro_manifest)
    copy('thor-map', retro / 'maps.json', 'retro_rewind/maps.json',
         (json.dumps(safe_manifest, indent=2) + '\n').encode('utf-8'))
    retro_files.append('maps.json')
    receipt['groups']['retro-rewind-maps'] = {'source': 'host-existing-verified-cache', 'host': str(retro),
        'files': retro_files, 'maps': len(retro_manifest['courses']), 'fallbacks': len(retro_manifest.get('failures', {})),
        'source_archive_hashes_preserved': True, 'png_hashes_verified': True,
        'metadata_adaptation': 'Removed only absolute host-path provenance/diagnostic string fields; visual pixels and course identity hashes unchanged.',
        'removed_metadata_fields': changes}
    entries.sort(key=lambda entry: entry['target'])
    if len(entries) > 8192 or sum(e['bytes'] for e in entries) > 768 * 1024 * 1024:
        raise ValueError('Presentation exceeds loader bounds')
    manifest = {'schema': 1, 'kind': 'wiids-bundled-presentation', 'revision': args.revision, 'files': entries}
    (staged / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    backup = BUILD / ('previous-' + uuid.uuid4().hex)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.rename(backup)
    try:
        staged.rename(output)
    except BaseException:
        if backup.exists() and not output.exists():
            backup.rename(output)
        raise
    receipt.update({'status': 'staged_verified_presentation_only', 'files': len(entries),
                    'bytes': sum(e['bytes'] for e in entries), 'manifest_sha256': sha(output / 'manifest.json'),
                    'output': str(output), 'backup': str(backup) if backup.exists() else None})
    (BUILD / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': receipt['status'], 'files': receipt['files'], 'bytes': receipt['bytes'],
                      'sources': {group: value['source'] for group, value in receipt['groups'].items()}}, indent=2))


if __name__ == '__main__':
    main()
