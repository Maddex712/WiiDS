"""One private PC setup/update flow. Plan is read-only; prepare is host-only.

Uses an owner's PAL image/extraction, already built signed APKs and optional
already extracted authorized RR pack. Never downloads, clears app data, changes
Config/controller mappings, or builds APKs. Device access requires --action install.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import uuid
import zipfile

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent.parent
# Reuse the repository's private parser dependencies without requiring a
# caller-specific PYTHONPATH (not installed or downloaded by this script).
LOCAL_PYTHON = ROOT / '.tools/map-python'
if LOCAL_PYTHON.is_dir(): sys.path.insert(0, str(LOCAL_PYTHON))
RUNTIME = ROOT / 'reference/driftdroid'
DISC = RUNTIME / 'DiscData'
PACKAGE = 'com.driftdroid.android.thor'
PRIVATE = 'files/WiiCompiled'
EXPECTED = {'sys/main.dol': '80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05',
            'files/rel/StaticR.rel': '16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d'}
CODE_SHA = '7471e1d2f61c11ab0de6544728406ee1359a4eab736b2dc6a73b741f69e7b4a3'


class MissingAssets(ValueError):
    pass


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while chunk := stream.read(1024 * 1024): digest.update(chunk)
    return digest.hexdigest()


def safe_relative(name):
    path = PurePosixPath(name)
    if (not name or path.is_absolute() or any(p in ('', '.', '..') for p in name.split('/'))
            or any(c in name for c in '\\\r\n\0:') or name.startswith('-')):
        raise ValueError(f'Unsafe relative resource path: {name!r}')
    return name


def regular(path, boundary):
    path, boundary = Path(path), Path(boundary).resolve()
    if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(boundary):
        raise ValueError(f'Nonregular or escaping input: {path}')
    return path


def tree_files(folder):
    folder = folder.resolve(strict=True)
    for path in sorted(folder.rglob('*')):
        if path.is_symlink() or not path.resolve().is_relative_to(folder):
            raise ValueError(f'Symlink/escaping input: {path}')
        if path.is_file():
            safe_relative(path.relative_to(folder).as_posix())
            yield path


def run(command, **kwargs):
    return subprocess.run([str(v) for v in command], check=True, **kwargs)


def load_script(name):
    spec = importlib.util.spec_from_file_location('thor_setup_' + name.replace('-', '_'), ROOT / 'scripts' / name)
    module = importlib.util.module_from_spec(spec)
    try: spec.loader.exec_module(module)
    except ModuleNotFoundError as error:
        raise ValueError(f'Local asset dependency {error.name!r} is missing; use the configured asset Python and existing .tools/map-python dependencies') from error
    return module


def check_disc(folder):
    for relative, expected in EXPECTED.items():
        if sha(regular(folder / relative, folder)) != expected:
            raise ValueError(f'Unsupported clean PAL executable: {relative}')


def disc_image(path):
    path = path.resolve(strict=True)
    with path.open('rb') as stream: header = stream.read(1024)
    # A complete raw ISO has its disc identity at byte zero. Compressed/WBFS
    # images need a separately verified conversion; do not guess their geometry.
    if len(header) != 1024 or header[:6] != b'RMCP01' or header[7] != 0:
        raise ValueError('Expected the owner\'s raw PAL RMCP01 revision-0 ISO')
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha(path)}


def apk_identity(path, java, signer, aapt):
    path = path.resolve(strict=True)
    signed = run([java, '-jar', signer, 'verify', '--print-certs', path], capture_output=True, text=True).stdout
    certificates = re.findall(r'Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]+)', signed)
    if len(certificates) != 1:
        raise ValueError('Expected one verified APK signing certificate')
    badging = run([aapt, 'dump', 'badging', path], capture_output=True, text=True, encoding='utf-8').stdout
    package = re.search(r"^package: name='([^']+)' versionCode='(\d+)'", badging, re.M)
    if not package or package[1] != PACKAGE or "native-code: 'arm64-v8a'" not in badging:
        raise ValueError(f'Expected the isolated arm64 Thor APK: {path}')
    with zipfile.ZipFile(path) as archive:
        libraries = {n: hashlib.sha256(archive.read(n)).hexdigest() for n in archive.namelist()
                     if n.startswith('lib/arm64-v8a/') and n.endswith('.so')}
    if 'lib/arm64-v8a/libWiiCompiled.so' not in libraries:
        raise ValueError('APK lacks the original-game runtime')
    return {'path': str(path), 'sha256': sha(path), 'certificate_sha256': certificates[0].lower(),
            'version_code': int(package[2]), 'debuggable': 'application-debuggable' in badging,
            'libraries': libraries}


def map_audit(pack=None):
    folder = ROOT / 'build/thor-map-data'
    retail = []
    for i in range(32):
        png, receipt = folder / f'course_{i:02}.png', folder / f'course_{i:02}.json'
        record = json.loads(regular(receipt, folder).read_text(encoding='utf-8'))
        source = DISC / 'files' / safe_relative(record['source'])
        if record['id'] != i or sha(regular(source, DISC)) != record['source_sha256']:
            raise ValueError(f'Retail map source/ID mismatch: {i}')
        actual = sha(regular(png, folder))
        if record.get('png_sha256', actual) != actual or (record.get('coverage') is not None and record['coverage'] <= 0):
            raise ValueError(f'Retail map output/coverage mismatch: {i}')
        retail.append({'id': i, 'source_sha256': record['source_sha256'], 'png_sha256': actual,
                       'coverage': record.get('coverage'), 'legacy_recipe_receipt': 'pipeline_sha256' not in record})
    report = {'retail': {'count': len(retail), 'courses': retail}, 'actual_all_course_gameplay_verified': False,
              'limits': ['Static geometry/roof filtering; general placed objects, animation and complex shaders remain limited.',
                        'Static audit is not all-course gameplay, complete-route or visual parity validation.']}
    hud = json.loads((folder / 'hud.json').read_text(encoding='utf-8'))
    portraits = hud['faces']
    if (len(portraits) != 24 or {r['id'] for r in portraits} != set(range(24))
            or len({r['sha256'] for r in portraits}) != 24
            or any(sha(regular(folder / r['texture'], folder)) != r['sha256'] for r in portraits)):
        raise ValueError('Portrait IDs, source outputs or uniqueness invalid')
    report['portraits'] = {'count': 24, 'unique_png_hashes': 24,
        'character_ids': {'Mario': 0, 'Baby Peach': 1, 'Baby Mario': 6, 'Peach': 16},
        'proof': 'Pinned PAL ResourceManager.hpp character IDs and private hud.json source TPL names',
        'note': 'Baby/adult portraits are distinct; their visual similarity is not duplicate telemetry evidence.'}
    if pack:
        stager = load_script('Stage-ThorCustomMaps.py')
        files = stager.validate()
        manifest = json.loads((stager.FOLDER / 'maps.json').read_text(encoding='utf-8'))
        if Path(manifest['pack_root']).resolve() != pack:
            raise ValueError('Custom-map source pack differs from selected pack; rebake it explicitly')
        baker = load_script('Bake-ThorCustomMaps.py')
        catalog, _ = baker.catalog(pack)
        all_keys = set(manifest['courses']) | set(manifest.get('failures', {}))
        if all_keys != set(catalog) or set(manifest['courses']) & set(manifest.get('failures', {})):
            raise ValueError('RR catalog has unrecorded/ambiguous course identities')
        for key, record in {**manifest['courses'], **manifest.get('failures', {})}.items():
            if record['source_sha256'] != catalog[key]['source_sha256']:
                raise ValueError(f'RR source SHA mismatch: {key}')
        report['retro_rewind'] = {'catalogued': len(catalog), 'maps': len(manifest['courses']),
            'map_bytes': sum(p.stat().st_size for p in files),
            'fallbacks': [dict(key=k, name=catalog[k]['name'], source_sha256=v['source_sha256'], reason=v['error'])
                          for k, v in manifest.get('failures', {}).items()],
            'identity': 'Exact successful archive basename AND compressed archive SHA-256, plus profile/Code.pul hash',
            'static_adaptations': {k: r.get('source_placement') for k, r in manifest['courses'].items() if r.get('source_placement')}}
    return report


def host_assets(pack, generate):
    env = dict(os.environ, PYTHONPATH=str(ROOT / '.tools/map-python'))
    needed = [('Bake-ThorMaps.py', ['--jobs', '2'], ROOT / 'build/thor-map-data/course_31.json'),
              ('Extract-ThorHud.py', [], ROOT / 'build/thor-map-data/hud.json'),
              ('Extract-ThorMenu.py', [], ROOT / 'build/thor-map-data/menu.json')]
    if pack:
        needed.append(('Bake-ThorCustomMaps.py', ['--pack-root', str(pack), '--all', '--jobs', '2', '--keep-going'],
                       ROOT / 'build/thor-rr-map-data/maps.json'))
    for script, flags, marker in needed:
        if not marker.is_file():
            if not generate: raise MissingAssets(f'Private assets missing; --generate-assets required: {marker}')
            result = subprocess.run([sys.executable, str(ROOT / 'scripts' / script), *flags], env=env)
            # Custom maps deliberately exit1 when explicit upper-HUD fallbacks exist.
            if result.returncode and not (script == 'Bake-ThorCustomMaps.py' and result.returncode == 1):
                raise ValueError(f'Asset command failed: {script}')
    run([sys.executable, ROOT / 'scripts/Stage-ThorMaps.py', '--check-only'], env=env, capture_output=True, text=True)
    menu = load_script('Stage-ThorMenu.py')
    menu.validate()


def resources(pack):
    result = [('DiscData/' + p.relative_to(DISC).as_posix(), p) for p in tree_files(DISC)]
    folder = ROOT / 'build/thor-map-data'
    # Only named generated sets; no arbitrary extra files from the asset directory.
    names = ['faces.json', 'items.json', 'hud.json']
    for i in range(32): names += [f'course_{i:02}.png', f'course_{i:02}.json']
    names += [f'face_{i:02}.png' for i in range(24)] + [f'item_{i:02}.png' for i in range(20)]
    hud = json.loads((folder / 'hud.json').read_text(encoding='utf-8'))
    names += list(hud['files'])
    result += [('ThorMap/' + safe_relative(n), regular(folder / n, folder)) for n in names]
    result += [('ThorMap/' + p.relative_to(folder).as_posix(), p) for p in load_script('Stage-ThorMenu.py').validate()]
    result += [('ThorMap/' + p.relative_to(folder).as_posix(), p) for p in load_script('Stage-ThorLauncher.py').validate()]
    # Optional launcher music/UI sounds rendered from the owner's own revo_kart.brsar.
    result += [('ThorMap/' + p.relative_to(folder).as_posix(), p) for p in load_script('Extract-ThorLauncherAudio.py').validate()]
    if pack:
        result += [('RetroRewind6/' + p.relative_to(pack).as_posix(), p) for p in tree_files(pack)]
        custom = load_script('Stage-ThorCustomMaps.py')
        result += [('ThorMap/retro_rewind/' + p.name, p) for p in custom.validate()]
    unique = {}
    for name, path in result:
        safe_relative(name)
        if name in unique and unique[name] != path: raise ValueError('Conflicting setup resources')
        unique[name] = path
    return sorted(unique.items())


def prepare_bundle(folder, records):
    archive = folder / 'resources.tar'
    hashes = []
    with tarfile.open(archive, 'w', format=tarfile.PAX_FORMAT) as output:
        for name, path in records:
            regular(path, DISC if name.startswith('DiscData/') else path.parent)
            checksum = sha(path)
            info = tarfile.TarInfo(name)
            info.size = path.stat().st_size
            info.mode = 0o600
            with path.open('rb') as stream: output.addfile(info, stream)
            # Catch inputs replaced while the package was being assembled.
            if sha(path) != checksum: raise ValueError(f'Input changed during packaging: {name}')
            hashes.append({'path': name, 'bytes': info.size, 'sha256': checksum})
        checks = ''.join(f"{r['sha256']}  {r['path']}\n" for r in hashes).encode()
        info = tarfile.TarInfo('.thor-setup.sha256'); info.size = len(checks)
        output.addfile(info, io.BytesIO(checks))
    return archive, hashes


def validate_backup(path):
    # Parse and consume the complete stream before allowing resource mutations.
    with tarfile.open(path, 'r:') as archive:
        for member in archive:
            safe_relative(member.name.rstrip('/'))
            if member.isfile():
                with archive.extractfile(member) as stream:
                    total = 0
                    while chunk := stream.read(1024 * 1024): total += len(chunk)
                if total != member.size: raise ValueError('Truncated private-data backup')
            elif not (member.isdir() or member.issym() or member.islnk()):
                raise ValueError('Unexpected special file in private-data backup')


def install(args, folder, report, archive, records, java, signer, aapt):
    if not args.serial: raise ValueError('--serial is mandatory for --action install')
    adb = [ROOT / '.tools/platform-tools/adb.exe', '-s', args.serial]
    def call(*command, **kwargs): return run([*adb, *command], **kwargs)
    def shell(command):
        # Fragments are constants, fixed resource roots or generated hex IDs.
        return call('shell', 'run-as', PACKAGE, 'sh', '-c', "'" + command + "'", capture_output=True, text=True)
    def journal():
        (folder / 'install-journal.json').write_text(json.dumps(report, indent=2) + '\n')
    def verify_package(identity):
        installed = call('shell', 'dumpsys', 'package', PACKAGE, capture_output=True, text=True).stdout
        debug = bool(re.search(r'pkgFlags=\[[^\]]*\bDEBUGGABLE\b', installed))
        if (f'Package [{PACKAGE}]' not in installed or 'pkgFlags=[' not in installed
                or debug != identity['debuggable']
                or not re.search(r'\bversionCode=' + str(identity['version_code']) + r'\b', installed)):
            raise ValueError('Installed package flags/version do not match the selected APK')
        listing = call('shell', 'pm', 'path', PACKAGE, capture_output=True, text=True).stdout.strip()
        lines = listing.splitlines()
        if len(lines) != 1 or not lines[0].startswith('package:/data/app/'):
            raise ValueError('Could not verify installed APK path')
        actual = folder / 'installed-check.apk'
        call('pull', lines[0][8:], actual, capture_output=True, text=True)
        if sha(actual) != identity['sha256']:
            raise ValueError('Installed APK readback differs from the verified signed input')
    keep = bool(getattr(args, 'keep_assets', False))
    free = call('shell', 'df', '-k', '/data', capture_output=True, text=True).stdout.splitlines()
    if len(free) < 2: raise ValueError('Could not verify free device storage')
    try: available = int(free[-1].split()[3]) * 1024
    except (ValueError, IndexError) as error: raise ValueError('Unrecognized device free-space report') from error
    if available < (0 if keep else report['resource_bytes'] * 2) + 512 * 1024 * 1024:
        raise ValueError('Insufficient free device space for staged data and retained backups')
    listing = call('shell', 'pm', 'path', PACKAGE, capture_output=True, text=True).stdout.strip()
    prior = None
    if listing:
        lines = listing.splitlines()
        if len(lines) != 1 or not lines[0].startswith('package:/data/app/'):
            raise ValueError('Split/unknown installed package layout; no update attempted')
        previous = folder / 'previous.apk'
        call('pull', lines[0][8:], previous, capture_output=True, text=True)
        prior = apk_identity(previous, java, signer, aapt)
        if prior['certificate_sha256'] != report['release_apk']['certificate_sha256']:
            raise ValueError('Existing signing identity differs; refusing update without uninstall')
        if prior['version_code'] > report['release_apk']['version_code']:
            raise ValueError('Installed version is newer; downgrades are not automatic')
        report['previous_apk'] = prior
    if keep and not prior: raise ValueError('--keep-assets requires an existing installation')
    token = uuid.uuid4().hex
    staging = PRIVATE + '/.thor-setup-' + token
    committed = []
    report.update(install_token=token, resource_mode='verify_existing' if keep else 'replace_declared_roots',
                  asset_journal=[], recovery_errors=[])
    journal()
    call('shell', 'am', 'force-stop', PACKAGE)
    try:
        # Include the install attempt: an ADB transport failure can happen after
        # the package manager has already committed the debug replacement.
        call('install', '-r', args.debug_apk, capture_output=True, text=True)
        shell('mkdir -p files')
        shell('for p in files files/WiiCompiled files/WiiCompiled/DiscData files/WiiCompiled/ThorMap files/WiiCompiled/RetroRewind6; do if [ -L "$p" ]; then exit 9; fi; done')
        backup_names = ['files']
        for name in ('shared_prefs', 'databases', 'no_backup'):
            probe = subprocess.run([str(x) for x in [*adb, 'shell', 'run-as', PACKAGE, 'test', '-d', name]], capture_output=True)
            if probe.returncode == 0: backup_names.append(name)
            elif probe.returncode != 1: raise ValueError('Could not inspect backup directory')
        # Omit reconstructible art/disc/pack trees; retain Config, NAND, saves,
        # riivolution saves, RR Ghosts/MyStuff and unknown generated RR entries.
        excluded = [PRIVATE + '/DiscData', PRIVATE + '/ThorMap']
        excluded += [PRIVATE + '/RetroRewind6/' + n for n in
                     ('Assets', 'Binaries', 'BT', 'Character', 'CT', 'Language', 'Patches', 'strm', 'Tracks', 'UI', 'xml')]
        backup = folder / 'app-private-before.tar'
        with backup.open('wb') as stream:
            call('exec-out', 'run-as', PACKAGE, 'tar', *('--exclude=' + p for p in excluded),
                 '-cf', '-', *backup_names, stdout=stream, timeout=3600)
        validate_backup(backup)
        report['private_backup'] = {'path': str(backup), 'bytes': backup.stat().st_size, 'sha256': sha(backup),
                                    'excluded_reconstructible_trees': excluded, 'fully_parsed': True}
        (folder / 'backup.json').write_text(json.dumps(report, indent=2) + '\n')
        journal()
        roots = sorted({r['path'].split('/')[0] for r in records})
        for name in roots:
            target = PRIVATE + '/' + name
            links = shell('if [ -d ' + target + ' ]; then find ' + target + ' -type l -print; fi').stdout.strip()
            if links: raise ValueError('Existing asset directory contains symlinks; refusing update')
        if keep:
            checks = ''.join(f"{r['sha256']}  {safe_relative(r['path'])}\n" for r in records).encode()
            call('exec-in', 'run-as', PACKAGE, 'sh', '-c',
                 "'cd " + PRIVATE + " && sha256sum -cs -'", input=checks, capture_output=True)
        else:
            shell('mkdir -p ' + staging)
            for name in ('ThorMap', 'RetroRewind6') if args.rr_pack else ('ThorMap',):
                target = PRIVATE + '/' + name
                shell('if [ -d ' + target + ' ]; then cp -a ' + target + ' ' + staging + '/' + name + '; fi')
            with archive.open('rb') as stream:
                call('exec-in', 'run-as', PACKAGE, 'tar', '-xf', '-', '-C', staging, stdin=stream, timeout=3600)
            shell('cd ' + staging + ' && sha256sum -cs .thor-setup.sha256')
            for name in roots:
                target, saved = PRIVATE + '/' + name, PRIVATE + '/' + name + '.before-' + token
                present = shell('if [ -e ' + target + ' ]; then echo yes; fi').stdout.strip() == 'yes'
                step = dict(target=target, saved=saved, previously_present=present, state='before_rename')
                report['asset_journal'].append(step); journal()
                # Register before the first rename so recovery also covers a
                # transport error after a successful device-side operation.
                committed.append((target, saved, present, step))
                if present: shell('mv ' + target + ' ' + saved)
                step['state'] = 'old_saved'; journal()
                shell('mv ' + staging + '/' + name + ' ' + target)
                step['state'] = 'new_committed'; journal()
            shell('cd ' + PRIVATE + ' && sha256sum -cs .thor-setup-' + token + '/.thor-setup.sha256')
        report['all_device_resource_hashes_verified'] = True
        call('install', '-r', args.release_apk, capture_output=True, text=True)
        verify_package(report['release_apk'])
    except BaseException as original:
        report['install_error'] = f'{type(original).__name__}: {original}'
        # Rollback is best-effort per root. A prior release install may already
        # have removed run-as, so regain the verified debug identity first.
        if committed:
            try: call('install', '-r', args.debug_apk, capture_output=True, text=True)
            except BaseException as error: report['recovery_errors'].append('debug access: ' + str(error))
            for target, saved, present, step in reversed(committed):
                try:
                    if present:
                        shell('if [ -e ' + saved + ' ]; then if [ -e ' + target + ' ]; then mv ' + target + ' ' + target + '.failed-' + token + '; fi; mv ' + saved + ' ' + target + '; fi')
                    else:
                        shell('if [ -e ' + target + ' ]; then mv ' + target + ' ' + target + '.failed-' + token + '; fi')
                    step['state'] = 'rollback_command_completed'
                except BaseException as error: report['recovery_errors'].append(target + ': ' + str(error))
        # APK recovery remains independent of directory rollback failures.
        release_recovery = dict(report['release_apk'], path=str(args.release_apk))
        candidates = [prior] if prior and not prior['debuggable'] else []
        if not candidates or prior['sha256'] != release_recovery['sha256']: candidates.append(release_recovery)
        for identity in candidates:
            try:
                path = Path(identity['path'])
                if sha(path) != identity['sha256']: raise ValueError('Recovery APK changed')
                flags = ['-r'] + (['-d'] if identity['version_code'] < report['release_apk']['version_code'] else [])
                call('install', *flags, path, capture_output=True, text=True)
                verify_package(identity)
                report['recovered_apk'] = {'sha256': identity['sha256'], 'debuggable': identity['debuggable'],
                                         'version_code': identity['version_code'], 'prior': identity is prior}
                break
            except BaseException as error: report['recovery_errors'].append('APK recovery: ' + str(error))
        try: journal()
        except OSError as error: report['recovery_errors'].append('Recovery journal: ' + str(error))
        raise
    report.update(device_serial=args.serial, status='installed_resources_verified_gameplay_unverified',
                  rollback_note='User-data host backup, previous APK and any device .before trees retained; no data cleared.')
    journal()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--action', choices=('plan', 'prepare', 'install'), default='plan')
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--disc', type=Path, help='Owner raw clean PAL RMCP01 revision0 ISO')
    source.add_argument('--use-extracted', action='store_true', help='Explicitly reuse the current verified local DiscData')
    parser.add_argument('--rr-pack', type=Path, help='Existing authorized extracted RetroRewind6; no download')
    parser.add_argument('--debug-apk', type=Path, default=ROOT / 'build/DriftDroid-Thor-debug.apk')
    parser.add_argument('--release-apk', type=Path, default=ROOT / 'build/DriftDroid-Thor.apk')
    parser.add_argument('--java', type=Path, default=shutil.which('java'))
    parser.add_argument('--serial', help='Required only for explicit install; no automatic device selection')
    parser.add_argument('--keep-assets', action='store_true', help='Install only: verify every existing declared resource hash and skip asset transfer/swaps')
    parser.add_argument('--generate-assets', action='store_true', help='Prepare missing original assets locally; RR all-course bake may take time')
    parser.add_argument('--report', type=Path, help='Optional JSON plan/audit output; otherwise plan writes no files')
    args = parser.parse_args()
    if args.action == 'install' and not args.serial:
        raise ValueError('--serial is mandatory for --action install')
    if args.keep_assets and args.action != 'install':
        raise ValueError('--keep-assets is only valid with --action install')
    if args.action == 'plan' and args.generate_assets:
        raise ValueError('Plan is read-only; use prepare/install to generate missing assets')
    if not args.java: raise ValueError('Java is required for APK signature verification; provide --java')
    signer = ROOT / '.tools/android-sdk/build-tools/34.0.0/lib/apksigner.jar'
    aapt = ROOT / '.tools/android-sdk/build-tools/34.0.0/aapt.exe'
    report = {'schema': 1, 'utc': datetime.now(timezone.utc).isoformat(), 'action': args.action,
              'package': PACKAGE, 'status': 'host_validation_started', 'device_contacted': False,
              'preserved': ['Config', 'controller mappings', 'saves', 'Mii/NAND data', 'preferences'],
              'prohibited_actions': ['download', 'uninstall', 'pm clear', 'automatic controller remap', 'guest mechanics changes']}
    report['debug_apk'] = apk_identity(args.debug_apk, args.java, signer, aapt)
    report['release_apk'] = apk_identity(args.release_apk, args.java, signer, aapt)
    debug, release = report['debug_apk'], report['release_apk']
    game_libraries = {'lib/arm64-v8a/libWiiCompiled.so', 'lib/arm64-v8a/libRetroRewind.so'}
    if (not debug['debuggable'] or release['debuggable'] or debug['certificate_sha256'] != release['certificate_sha256']
            or debug['version_code'] != release['version_code']
            or any(debug['libraries'].get(n) != release['libraries'].get(n) for n in game_libraries)):
        raise ValueError('Need matching locally built debug/release Thor APKs, one certificate/version/native payload')
    report['variant_support_library_differences'] = [n for n in set(debug['libraries']) | set(release['libraries'])
                                                   if debug['libraries'].get(n) != release['libraries'].get(n)]
    pack = args.rr_pack.resolve(strict=True) if args.rr_pack else None
    if pack:
        if sha(regular(pack / 'Binaries/Code.pul', pack)) != CODE_SHA or 'lib/arm64-v8a/libRetroRewind.so' not in release['libraries']:
            raise ValueError('Unsupported RR Code.pul or APK lacks standalone RR runtime')
        report['rr_pack'] = {'path': str(pack), 'code_pul_sha256': CODE_SHA}
    if args.disc:
        report['disc_image'] = disc_image(args.disc)
        if DISC.exists():
            marker = json.loads((ROOT / 'build/disc-validation.json').read_text(encoding='utf-8-sig'))
            if marker.get('imageSha256') != report['disc_image']['sha256']:
                raise ValueError('Current extraction belongs to another ISO; it will not be overwritten')
        elif args.action != 'plan':
            scratch = ROOT / 'build' / ('thor-disc-extract-' + uuid.uuid4().hex)
            run([ROOT / '.tools/dtk.exe', 'disc', 'extract', '--validate', args.disc, scratch])
            check_disc(scratch)
            scratch.rename(DISC)
            marker = dict(gameId='RMCP01', imageSha256=report['disc_image']['sha256'],
                          dolSha256=EXPECTED['sys/main.dol'], relSha256=EXPECTED['files/rel/StaticR.rel'])
            (ROOT / 'build/disc-validation.json').write_text(json.dumps(marker, indent=2) + '\n')
    if DISC.is_dir():
        check_disc(DISC)
        try:
            host_assets(pack, args.generate_assets if args.action != 'plan' else False)
            report['map_audit'] = map_audit(pack)
        except (FileNotFoundError, MissingAssets) as error:
            if args.action != 'plan': raise
            report['asset_generation_needed'] = str(error)
    elif not args.disc: raise ValueError('Verified local DiscData is missing')
    else: report['asset_generation_needed'] = 'ISO extraction and original-asset generation required'
    report['steps'] = ['Validate PAL identity, matching APK signatures/profile/version/native payload and optional RR code',
        'Prepare private checksummed original data/art bundle; no downloads/builds',
        'Explicit install only: verify installed certificate/version, force-stop, same-signed debug install -r',
        'Back up protected user data excluding reconstructible asset trees; fully parse the host backup',
        'Verify existing assets without transfer when --keep-assets; otherwise stage and verify every resource hash',
        'Rename only DiscData/ThorMap/optional RetroRewind6 with rollback; never Config/saves/mappings',
        'Same-signed release install -r; verify committed hashes and non-debuggable package; retain backups']
    report['status'] = 'read_only_plan_no_device_contact'
    if args.action != 'plan':
        folder = ROOT / 'build' / ('thor-setup-' + uuid.uuid4().hex)
        folder.mkdir()
        try:
            source_files = resources(pack)
            source_bytes = sum(path.stat().st_size for _, path in source_files)
            required = (0 if args.keep_assets else source_bytes * 2) + args.debug_apk.stat().st_size + args.release_apk.stat().st_size + 1024**3
            if shutil.disk_usage(folder).free < required:
                raise ValueError('Insufficient host space for bundle, APK snapshots and user-data backup')
            if args.keep_assets:
                archive = None
                records = [dict(path=n, bytes=p.stat().st_size, sha256=sha(p)) for n, p in source_files]
            else:
                archive, records = prepare_bundle(folder, source_files)
            report.update(status='prepared_host_only', bundle=str(archive) if archive else None, bundle_sha256=sha(archive) if archive else None,
                          resource_count=len(records), resource_bytes=sum(r['bytes'] for r in records), resources=records)
            if args.action == 'install':
                # Freeze the two verified APK inputs before any device operation.
                for role in ('debug', 'release'):
                    source = getattr(args, role + '_apk')
                    snapshot = folder / (role + '.apk')
                    shutil.copyfile(source, snapshot)
                    if sha(snapshot) != report[role + '_apk']['sha256']:
                        raise ValueError('APK changed during preparation; no device operation attempted')
                    setattr(args, role + '_apk', snapshot)
                report['device_contacted'] = True
                install(args, folder, report, archive, records, args.java, signer, aapt)
        except BaseException as error:
            report.update(status='incomplete', error=f'{type(error).__name__}: {error}')
            (folder / 'setup.json').write_text(json.dumps(report, indent=2) + '\n')
            raise
        (folder / 'setup.json').write_text(json.dumps(report, indent=2) + '\n')
        report['receipt'] = str(folder / 'setup.json')
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('resources', 'debug_apk', 'release_apk')}, indent=2))


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError, tarfile.TarError, subprocess.CalledProcessError) as error:
        print(f'Setup stopped: {error}', file=sys.stderr)
        raise SystemExit(2)
