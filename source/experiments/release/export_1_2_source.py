"""Inventory the private 1.2 source profile; optionally write a source-only review ZIP.

Default is a dry run. Never invokes Gradle, signing, installation, Git mutation, or publication.
External inputs are explicitly named source roots, not arbitrary private-directory walks.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile

_spec = importlib.util.spec_from_file_location('wiids_export_policy', Path(__file__).with_name('export_release_source.py'))
old = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(old)
_asset_spec = importlib.util.spec_from_file_location('wiids_runtime_assets', Path(__file__).with_name('stage_runtime_assets_1_2.py'))
runtime_assets = importlib.util.module_from_spec(_asset_spec)
_asset_spec.loader.exec_module(runtime_assets)

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PRIVATE = Path('M:/WiiDS-release-wrap-2026-11-08')
MKDD = Path('M:/WiiDS-mkdd-2026-11-01/runtime')
BRAWL = Path('M:/WiiDS-brawl/src')
ARCHIVES = Path('M:/WiiDS-mkdd-2026-11-01/all-game-packs-2026-11-04/deps')
PIN_MKDD = 'bd36eb982ea58c6651584cc48264876d1aba9546'
PIN_GCN = 'b2193d7c7ce65c40959cf81859beaacafeebfd99'
DEFAULT_APK_SHA = '49c54110846b9d2f056ae2ad1973ee16c923da0e39784f301ba2c5871c0e9cdc'
BRANDING_SHA = 'ca8eb3fb32cb80fcb1a099c02d3bc822345257cb47f81327095a2faf19c59d3f'
SKIP_PARTS = old.INFRASTRUCTURE_PARTS | {
    'bin', 'obj',
    'generated', 'native-build', 'build', 'build-debug', 'build-release', 'build-logs',
    'discdata', 'extracted', 'card-a', 'card-b', 'case-art', 'retail-art', 'source-art', 'supplied-metadata',
    'private-before', 'screenshots', 'play', 'dependencies',
}
PRIVATE_AT_ROOT = {'rom', 'roms', 'game', 'games', 'nand', 'saves', 'save', 'cache', 'logs', 'before'}
OMIT_NAMES = old.DENIED_NAMES | {
    'codex_handoff.md', 'claude_handoff.md', 'claude_bottom_screen_handoff.md',
    'agents.md', 'claude.md', 'resume.txt', 'settings.txt', 'config.toml', 'brawl_module_layout.inc',
}
GENERATED = re.compile(r'^(?:ppc_func_|data_sections_(?:init|runtime)|shard_|module_[0-9]+_)', re.I)


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def metadata(path):
    result = {}
    for key, command in [('commit', ('rev-parse', 'HEAD')), ('remote', ('config', '--get', 'remote.origin.url'))]:
        run = subprocess.run(['git', '-C', str(path), *command], capture_output=True, text=True)
        value = run.stdout.strip() if run.returncode == 0 else None
        if key == 'remote' and value and not re.fullmatch(r'https://github\.com/[\w.-]+/[\w.-]+(?:\.git)?', value):
            value = None
        result[key] = value
    return result


def allowed(relative, *, galaxy=False):
    p = PurePosixPath(relative)
    parts = [x.lower() for x in p.parts]
    if (parts[0] in PRIVATE_AT_ROOT or
            any(x in SKIP_PARTS or x.startswith('cmake-build-') for x in parts[:-1])):
        return False
    if 'assets' in parts[:-1] or ('decomp' in parts[:-1] and not galaxy):
        return False
    if p.name.lower() in OMIT_NAMES or p.name.startswith('.env') or GENERATED.match(p.name):
        return False
    return p.suffix.lower() in old.EXTENSIONS or p.name in old.NAMES or p.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE'))


def walk(directory, *, galaxy=False):
    for parent, dirs, files in os.walk(directory, followlinks=False):
        relparent = Path(parent).relative_to(directory)
        dirs[:] = sorted(d for d in dirs if not (Path(parent) / d).is_symlink()
                         and d.lower() not in SKIP_PARTS and d.lower() != 'assets'
                         and (relparent != Path('.') or d.lower() not in PRIVATE_AT_ROOT)
                         and (d.lower() != 'decomp' or galaxy) and not d.startswith('cmake-build-'))
        for name in sorted(files):
            relative = (relparent / name).as_posix()
            if allowed(relative, galaxy=galaxy):
                yield relative, Path(parent) / name


def paused_inventory():
    delta = json.loads((ROOT / 'build/ra-casual/source-delta.json').read_text())['files']
    if len(delta) != 91:
        raise ValueError('The paused source inventory must contain exactly 91 entries')
    for item in delta:
        if sha(ROOT / item['path']) != item['after_sha256']:
            raise ValueError('Paused source changed: ' + item['path'])
    return {item['path']: item for item in delta}


def approved_presentation(private, apk):
    """The previous source export's narrow owner-approved manifest exception, checked against this APK."""
    folder = private / 'android/app/src/main/assets/presentation'
    manifest_path = folder / 'manifest.json'
    raw = manifest_path.read_bytes()
    manifest = json.loads(raw)
    if manifest.get('schema') != 1 or manifest.get('kind') != 'wiids-bundled-presentation':
        raise ValueError('Unknown approved presentation manifest')
    files, revision = manifest.get('files'), manifest.get('revision')
    if not isinstance(files, list) or not 1 <= len(files) <= 8192 or not isinstance(revision, str) or not re.fullmatch('[A-Za-z0-9_.-]{1,96}', revision):
        raise ValueError('Invalid presentation inventory/revision')
    selected, expected, targets, total = {}, {'manifest.json'}, set(), 0
    with zipfile.ZipFile(apk) as z:
        if z.read('assets/presentation/manifest.json') != raw:
            raise ValueError('Android presentation manifest differs from the chosen candidate APK')
        for item in files:
            target, asset = item.get('target'), item.get('asset')
            if not isinstance(target, str) or not isinstance(asset, str):
                raise ValueError('Invalid presentation member')
            path = PurePosixPath(target)
            if (target.startswith('/') or '\\' in target or path.as_posix() != target or
                    any(part in ('', '.', '..') for part in target.split('/')) or len(path.parts) < 2 or
                    path.parts[0] not in old.PRESENTATION_TARGETS or path.suffix.lower() not in old.PRESENTATION_SUFFIXES or
                    asset != 'presentation/files/' + target or target in targets):
                raise ValueError('Unsafe, unapproved, or duplicate presentation target')
            size, digest = item.get('bytes'), item.get('sha256')
            if type(size) is not int or not 0 < size <= 128 * 1024 * 1024 or not isinstance(digest, str) or not re.fullmatch('[a-f0-9]{64}', digest):
                raise ValueError('Invalid presentation hash/size')
            relative = 'files/' + target
            source = folder / relative
            if source.is_symlink() or folder.resolve() not in source.resolve().parents:
                raise ValueError('Presentation symlink/path escape')
            data = source.read_bytes()
            if len(data) != size or hashlib.sha256(data).hexdigest() != digest or z.read('assets/' + asset) != data:
                raise ValueError('Approved presentation differs from manifest/APK: ' + target)
            suffix = path.suffix.lower()
            if ((suffix == '.png' and not data.startswith(b'\x89PNG\r\n\x1a\n')) or
                    (suffix in ('.jpg', '.jpeg') and not data.startswith(b'\xff\xd8\xff')) or
                    (suffix == '.wav' and not (data.startswith(b'RIFF') and data[8:12] == b'WAVE'))):
                raise ValueError('Invalid presentation media signature')
            if suffix == '.json':
                json.loads(data)
            if suffix in ('.json', '.tsv') and old.SECRETS.search(data):
                raise ValueError('Possible secret in presentation metadata')
            selected['build/wiids-merge/assets/presentation/' + relative] = {
                'path': source, 'sha256': digest, 'bytes': size, 'kind': 'approved-presentation'}
            expected.add(relative)
            targets.add(target)
            total += size
            if total > 768 * 1024 * 1024:
                raise ValueError('Approved presentation exceeds the prior source export size limit')
    actual = {x.relative_to(folder).as_posix() for x in folder.rglob('*') if x.is_file()}
    if actual != expected:
        raise ValueError('Presentation folder contains unlisted or missing files')
    selected['build/wiids-merge/assets/presentation/manifest.json'] = {
        'path': manifest_path, 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw), 'kind': 'approved-presentation-manifest'}
    return selected, {'revision': revision, 'files': len(files), 'bytes': total,
                      'manifest_sha256': hashlib.sha256(raw).hexdigest(), 'all_members_match_candidate': True,
                      'authority': 'Existing owner-approved explicit presentation policy; attribution retained in WIIDS-NOTICE.md; no new artwork permissions inferred.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--private-root', type=Path, default=DEFAULT_PRIVATE)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--apk', type=Path)
    parser.add_argument('--expected-apk-sha256', default=DEFAULT_APK_SHA)
    parser.add_argument('--candidate-audit', type=Path, help='Root build audit binding this APK and Gradle-stripped native receipts')
    parser.add_argument('--mkdd-depth-native', type=Path, help='Clean follow-up MKDD/Plus link receipts and renderer source')
    parser.add_argument('--mkdd-controller-native', type=Path, help='MKDD/Plus controller relink receipts and libraries')
    parser.add_argument('--galaxy-native', type=Path, help='Clean Galaxy build receipts and optional Vulkan copy/descriptor sources')
    parser.add_argument('--archive', action='store_true', help='Write a source-only review ZIP after inventory; never a public release')
    args = parser.parse_args()
    if args.mkdd_controller_native and not args.mkdd_depth_native:
        parser.error('--mkdd-controller-native requires the baseline --mkdd-depth-native receipts')
    private = args.private_root.resolve()
    candidate_audit = args.candidate_audit or private / 'candidate3-audit.json'
    output = (args.output_dir or private / 'package-review').resolve()
    if output != private / 'package-review' and private / 'package-review' not in output.parents:
        parser.error('Output must stay inside the selected private package-review directory')
    output.mkdir(parents=True, exist_ok=True)
    delta = paused_inventory()
    entries, roots, blockers, choices = {}, [], [], {}
    native_overrides = {
        'reference/galaxy-android/platform/src/gx/' + name: private / 'native' / name
        for name in ('gx_recorder_tex.cpp', 'vk_shadergen_fs.cpp', 'vk_shadergen_tables.cpp', 'gx_recorder.cpp')
    }
    native_overrides['reference/galaxy-android/platform/src/gx/vk/vk_sampler_manager.cpp'] = private / 'native/vk_sampler_manager.cpp'
    galaxy_vk_sources = ('vk_target_manager.cpp', 'vk_descriptor_manager.cpp', 'vk_copy_dispatch.cpp')
    if args.galaxy_native:
        for name in galaxy_vk_sources:
            native_overrides['reference/galaxy-android/platform/src/gx/vk/' + name] = args.galaxy_native / name
    native_overrides['third_party/mariokartdd-recomp/gcn-recomp/runtime/gx/texture.cpp'] = private / 'native/texture.cpp'
    if args.mkdd_depth_native:
        native_overrides['third_party/mariokartdd-recomp/gcn-recomp/runtime/gx/render_gl.cpp'] = args.mkdd_depth_native / 'render_gl.cpp'

    def add(name, path, *, paused=True):
        if name in entries:
            raise ValueError('Duplicate source archive path: ' + name)
        if name in native_overrides:
            path = native_overrides[name]
            choices[name] = 'exact current private native compile input; final candidate correspondence still requires review'
        if path.is_symlink():
            raise ValueError('Source symlink refused: ' + name)
        if paused and name in delta:
            item = delta[name]
            if item['before_sha256'] is None:
                choices[name] = 'omit paused experiment addition'
                return
            # Integrated Android already contains reviewed post-1.1 production changes.
            # Never replace that whole file with an older pre-experiment version.
            if not name.startswith('reference/driftdroid/android/'):
                path = ROOT / 'build/ra-casual/before/source' / name
                if sha(path) != item['before_sha256']:
                    raise ValueError('Paused restoration baseline changed: ' + name)
                choices[name] = 'pre-experiment source substitution'
            else:
                choices[name] = 'exact integrated Android source; paused workbench left untouched'
        size = path.stat().st_size
        if size > 16 * 1024 * 1024:
            raise ValueError('Unexpected large source requires review: ' + name)
        data = path.read_bytes()
        if b'\0' in data or old.SECRETS.search(data):
            raise ValueError('Binary or possible secret refused: ' + name)
        try:
            data.decode('utf-8-sig')
        except UnicodeDecodeError:
            if not name.startswith('third_party/') or any(c < 32 and c not in (9, 10, 12, 13) for c in data):
                raise ValueError('Non-UTF-8 input requires review: ' + name) from None
        entries[name] = {'path': path, 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': size}

    def tree(prefix, directory, *, galaxy=False, skip_android=False):
        if not directory.is_dir():
            blockers.append('Missing source root: ' + prefix)
            return
        count = 0
        for relative, path in walk(directory, galaxy=galaxy):
            if skip_android and (relative == 'android' or relative.startswith('android/')):
                continue
            add(prefix + '/' + relative, path)
            count += 1
        # Independent source coverage enumeration; no call to walk()/allowed().
        omitted = []
        for parent, dirs, files in os.walk(directory, followlinks=False):
            at_root = Path(parent) == directory
            dirs[:] = [d for d in dirs if d.lower() not in SKIP_PARTS and d.lower() != 'assets'
                       and (not at_root or d.lower() not in PRIVATE_AT_ROOT)
                       and (d.lower() != 'decomp' or galaxy) and not d.startswith('cmake-build-')
                       and not (Path(parent) / d).is_symlink()]
            for name in files:
                relative = (Path(parent) / name).relative_to(directory).as_posix()
                key = prefix + '/' + relative
                if skip_android and relative.startswith('android/'):
                    continue
                if Path(name).suffix.lower() not in old.SOURCE_SUFFIXES or name.lower() in OMIT_NAMES or GENERATED.match(name):
                    continue
                if key in delta and delta[key]['before_sha256'] is None:
                    continue
                if key not in entries:
                    omitted.append(key)
        if omitted:
            raise ValueError('Independent source coverage omissions: ' + json.dumps(omitted))
        roots.append({'archive_root': prefix, 'files_enumerated': count, 'coverage_omissions': omitted})

    for name in old.REPOS:
        tree('reference/' + name, ROOT / 'reference' / name, galaxy=name == 'galaxy-android', skip_android=name == 'driftdroid')
    tree('reference/driftdroid/android', private / 'android')
    for name in (*old.LOCAL_ROOTS, 'experiments/mkdd-thor', 'experiments/texture-packs'):
        tree(name, ROOT / name)
    for target, (location, remote, pin) in old.DEPENDENCIES.items():
        tree(target, ROOT / location)
    for name in ('LICENSE', *old.EXTRA_SOURCE):
        p = ROOT / name
        if p.is_file():
            add(name, p)
        else:
            blockers.append('Missing explicit input: ' + name)

    # Actual native build source, including local modifications; never the private ROM/output roots.
    tree('third_party/mariokartdd-recomp', MKDD)
    for prefix in ('reference/galaxy-android/platform/src/gx', 'third_party/mariokartdd-recomp/gcn-recomp/runtime/gx'):
        for name in ('texture_pack.h', 'stb_image.h', 'bcdec.h'):
            add(prefix + '/' + name, private / 'native' / name, paused=False)
    mkdd_meta, gcn_meta = metadata(MKDD), metadata(MKDD / 'gcn-recomp')
    for label, actual, expected in [('mariokartdd-recomp', mkdd_meta['commit'], PIN_MKDD), ('gcn-recomp', gcn_meta['commit'], PIN_GCN)]:
        if actual != expected:
            blockers.append(label + ' upstream pin differs or unavailable: ' + str(actual))
    for component in ('runtime', 'translator', 'aurora-main', 'projects/brawl/tools', 'projects/brawl/gecko'):
        tree('third_party/brawl/wiicompiled/' + component, BRAWL / 'wiicompiled' / component)
    for name in ('LICENSE', 'THIRD-PARTY-NOTICES.md'):
        add('third_party/brawl/wiicompiled/' + name, BRAWL / 'wiicompiled' / name)
    add('third_party/brawl/wiicompiled/projects/brawl/recomp.yml', BRAWL / 'wiicompiled/projects/brawl/recomp.yml')
    brawl_config = BRAWL / 'extras/brawl-decomp'
    tree('third_party/brawl-decomp-config/RSBE01_02', brawl_config / 'config/RSBE01_02')
    add('third_party/brawl-decomp-config/LICENSE', brawl_config / 'LICENSE')
    tree('third_party/Gecko-codehandler', private / 'package-review/upstream-codehandler')
    tree('third_party/brawl/android-integration', Path('M:/WiiDS-brawl-merged-20261029/native'))
    for name in ('Configure-BrawlAndroid.ps1', 'Stage-BrawlNative.ps1', 'brres_tex.py', 'extract_fonts.py', 'stage_lower_assets.py'):
        add('third_party/brawl/build-tools/' + name, Path('M:/WiiDS-brawl/scripts') / name)
    for version in ('libarchive-3.8.11', 'xz-5.8.4'):
        tree('third_party/' + version, ARCHIVES / version)

    # Text notices only: no presentation, model, texture, audio, save, key, native, or shader-cache payloads.
    tree('release/licenses/apk-1.2', private / 'android/app/src/main/assets/licenses')
    if (output / 'licenses').is_dir():
        tree('release/licenses/apk-1.2-review-additions', output / 'licenses')
    if any(name not in entries or name not in choices for name in native_overrides):
        raise ValueError('A required native source overlay was not applied at its real source path')
    for name in ('preview.gradle',):
        add('experiments/release/private-1.2-build/' + name, private / name, paused=False)
    apk = args.apk or private / 'WiiDS-1.2-candidate3.apk'
    if sha(apk) != args.expected_apk_sha256:
        raise ValueError('Candidate APK hash mismatch; specify the separately verified replacement hash')
    # Named authored asset, already explicitly included by the previous source export policy.
    branding = ROOT / 'build/branding/wiids-thor-minimal-v4.png'
    if sha(branding) != BRANDING_SHA:
        raise ValueError('Previously approved master branding image changed')
    entries['build/branding/wiids-thor-minimal-v4.png'] = {
        'path': branding, 'sha256': BRANDING_SHA, 'bytes': branding.stat().st_size, 'kind': 'approved-master-branding'}
    presentation, presentation_receipt = approved_presentation(private, apk)
    for name, item in presentation.items():
        if name in entries:
            raise ValueError('Presentation path collides with source inventory')
        entries[name] = item
    # Explicitly reviewed non-presentation runtime inputs only. Galaxy code ZIPs
    # regenerate from source and pinned dependencies; no compiled wheels go in this ZIP.
    supplemental = runtime_assets.prepare_inputs(apk, output / 'runtime-asset-inputs')
    for name, item in supplemental.items():
        if name in entries:
            raise ValueError('Runtime asset input collides with source inventory')
        entries[name] = item
    with zipfile.ZipFile(apk) as archive:
        native = {name: hashlib.sha256(archive.read(name)).hexdigest() for name in archive.namelist() if name.startswith('lib/') and name.endswith('.so')}
        notices = {name: hashlib.sha256(archive.read(name)).hexdigest() for name in archive.namelist() if name.startswith('assets/licenses/') and not name.endswith('/')}
        for asset in ('content/catalog.json', 'content/msmb-content.txt'):
            source = private / 'android/app/src/main/assets' / asset
            if archive.read('assets/' + asset) != source.read_bytes():
                raise ValueError('Authored content metadata differs from candidate: ' + asset)
            add('reference/driftdroid/android/app/src/main/assets/' + asset, source, paused=False)
    candidate_mismatches = []
    license_root = private / 'android/app/src/main/assets/licenses'
    for source in license_root.rglob('*'):
        if source.is_file():
            key = 'assets/licenses/' + source.relative_to(license_root).as_posix()
            if notices.get(key) != sha(source):
                candidate_mismatches.append(key)
    native_correspondence = {}
    if candidate_audit.is_file():
        audit = json.loads(candidate_audit.read_text())
        if audit['sha256'] != args.expected_apk_sha256 or audit['libraries'] != native:
            raise ValueError('Root candidate audit does not describe this exact APK/native inventory')
        normalized_root = output / 'native-normalized'
        normalized_root.mkdir(exist_ok=True)
        strip = ROOT / '.tools/android-sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
        for target, library_name in [('galaxy', 'libgame.so'), ('mkdd', 'libwiids_mkdd_game.so'), ('plus', 'libwiids_mkdd_plus_game.so')]:
            native_root = args.galaxy_native if target == 'galaxy' and args.galaxy_native else private / 'native'
            receipt = json.loads((native_root / target / 'verification.json').read_text())
            source_files = {}
            prefix = 'reference/galaxy-android/platform/src/gx/' if target == 'galaxy' else 'third_party/mariokartdd-recomp/gcn-recomp/runtime/gx/'
            sources = dict(receipt['source_sha256'])
            for path, digest in receipt.get('extra_sources', {}).items():
                sources[Path(path).name] = digest
            if target != 'galaxy' and args.mkdd_depth_native:
                followup = json.loads((args.mkdd_depth_native / target / 'verification.json').read_text())
                baseline = Path(receipt['package'])
                if sha(baseline) != receipt['new_sha256']:
                    raise ValueError('Initial native build differs from its source receipt: ' + target)
                normalized = normalized_root / ('baseline-' + library_name)
                subprocess.run([str(strip), '--strip-unneeded', '-o', str(normalized), str(baseline)], check=True)
                if sha(normalized) != followup['reference_apk_library_sha256']:
                    raise ValueError('Renderer follow-up does not start from the reviewed native library: ' + target)
                sources['render_gl.cpp'] = followup['renderer_sha256']
                receipt = followup
            for name, digest in sources.items():
                archive_name = prefix + ('vk/' if name in ('vk_sampler_manager.cpp', *galaxy_vk_sources) else '') + name
                if archive_name not in entries or entries[archive_name]['sha256'] != digest:
                    raise ValueError('Exported native source differs from the actual compile receipt: ' + archive_name)
                source_files[archive_name] = digest
            built = Path(receipt['package'])
            if sha(built) != receipt['new_sha256']:
                raise ValueError('Native build output differs from its source receipt: ' + target)
            normalized = normalized_root / library_name
            subprocess.run([str(strip), '--strip-unneeded', '-o', str(normalized), str(built)], check=True)
            packaged_hash = native['lib/arm64-v8a/' + library_name]
            built_hash = receipt['new_sha256']
            if target != 'galaxy' and args.mkdd_controller_native:
                controller_root = args.mkdd_controller_native / target
                controller = json.loads((controller_root / 'verification.json').read_text())
                if sha(normalized) != controller['prior_apk_library']:
                    raise ValueError('Controller relink does not match the reviewed renderer baseline: ' + target)
                for name, key in [('flat_main.cpp', 'source'), ('controller_ports.h', 'controller_ports')]:
                    archive_name = 'experiments/mkdd-thor/native/' + name
                    if entries[archive_name]['sha256'] != controller[key]:
                        raise ValueError('Exported controller source differs from its compile receipt: ' + archive_name)
                    source_files[archive_name] = controller[key]
                built_hash = sha(controller_root / library_name)
                if built_hash != controller['output'] or packaged_hash != controller['apk_library']:
                    raise ValueError('Controller relink output differs from its receipt: ' + target)
                subprocess.run([str(strip), '--strip-unneeded', '-o', str(normalized),
                                str(controller_root / library_name)], check=True)
            if sha(normalized) != packaged_hash or not audit['native_correspondence'][target]['gradle_strip_unneeded_exact']:
                raise ValueError('Built native library does not match the chosen APK after Gradle stripping: ' + target)
            native_correspondence[target] = {'source_files': source_files, 'built_sha256': built_hash,
                                              'packaged_sha256': packaged_hash, 'gradle_strip_unneeded_exact': True}
    pending = [
        'Reconcile every native library and DEX with the selected final candidate build/source receipts; changed Galaxy/MKDD/Plus native sources are checked when --candidate-audit is supplied.',
        'Rebuild from an isolated source snapshot; this inventory does not establish a clean-machine build.',
        'Regenerate all omitted disc/game-derived inputs privately from supported owner discs; audit Brawl original-byte/generated-input boundaries.',
        'Run prepare_brawl_1_2.py with the owner disc and exported public symbol metadata to regenerate excluded maps/module layout; exact retained-input parity was verified privately.',
        'The exact 602 content assets can be materialized from the reviewed presentation, authored metadata, 17 pinned runtime blobs, and Galaxy ZIP source recipes with pinned upstream dependencies.',
        'The catalog archive CMake/build recipe builds from verified pinned tarballs; reconcile build flags/library identity if replacing the already shipped library.',
        'Validate current Galaxy shader/texture patches against the actual native source and final replacement library.',
        'Choose and verify the internal version code for local 1.2; no public refs, uploads, or announcements are authorized.',
    ]
    manifest = {'format': 'wiids-private-1.2-source-review-v1', 'dry_run': not args.archive,
                'status': 'REVIEW_REQUIRED', 'published': False, 'version_name': '1.2',
                'candidate_apk': {'sha256': args.expected_apk_sha256, 'bytes': apk.stat().st_size, 'native_libraries': native, 'license_assets': notices},
                'presentation_assets': presentation_receipt,
                'runtime_asset_kit': {'remaining_paths': len(runtime_assets.METADATA['files']), 'reviewed_inputs': len(supplemental),
                                      'total_content_assets': runtime_assets.METADATA['content_asset_count'],
                                      'recipe': 'experiments/release/stage_runtime_assets_1_2.py'},
                'source_roots': roots, 'paused_files_verified_unchanged': len(delta), 'source_choices': choices,
                'pins': {'mkdd': {**mkdd_meta, 'expected': PIN_MKDD}, 'gcn': {**gcn_meta, 'expected': PIN_GCN}, 'brawl': metadata(BRAWL),
                         'brawl_public_symbols': {**metadata(brawl_config), 'expected': 'af23077265c70b833c8604a51eba9b64b6381c54'},
                         'repositories': {name: metadata(ROOT / 'reference' / old.DERIVED.get(name, name)) for name in old.REPOS},
                         'dependencies': {target: {'upstream': remote, 'expected_pin': pin, **metadata(ROOT / location)}
                                          for target, (location, remote, pin) in old.DEPENDENCIES.items()},
                         'archive_sources_sha256': {'libarchive-3.8.11.tar.gz': '900fe9fc7237c2b04f090f3c3648de8fe597525771f5035016537ce2d667d12b',
                                                   'xz-5.8.4.tar.gz': '0014c7886930454fe8bd4228665b51af55eeae560ea135c9c4cd33f55b2591d9'}},
                'coverage_omissions': [], 'missing_inputs': blockers, 'review_items': pending,
                'candidate_license_mismatches': candidate_mismatches,
                'verified_changed_native_correspondence': native_correspondence,
                'excludes': 'All binary/private assets except the exact approved presentation, named master branding, and 17 fixed-hash licensed runtime inputs; discs; generated translations; extracted sections; generated address maps; saves; keys; native binaries; build outputs; paused experiment additions',
                'files': {name: {key: value for key, value in item.items() if key != 'path'} for name, item in sorted(entries.items())}}
    if args.archive:
        if blockers:
            raise ValueError('Missing inputs prevent source ZIP creation: ' + json.dumps(blockers))
        if candidate_mismatches:
            raise ValueError('Choose the rebuilt candidate containing the reviewed notices before writing its matching source ZIP: ' + json.dumps(candidate_mismatches))
        if not native_correspondence:
            raise ValueError('--archive requires --candidate-audit and verified current native source/build correspondence')
        target = output / 'WiiDS-1.2-source-review.zip'
        if target.exists():
            raise ValueError('Refusing to overwrite an existing source review ZIP')
        temporary = target.with_suffix('.zip.new')
        if temporary.exists():
            raise ValueError('Refusing an existing temporary ZIP')
        try:
            with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
                for name, item in sorted(entries.items()):
                    data = item['path'].read_bytes()
                    if hashlib.sha256(data).hexdigest() != item['sha256']:
                        raise ValueError('Source changed during export: ' + name)
                    info = zipfile.ZipInfo(name, date_time=(2026, 11, 8, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = 0o100644 << 16
                    archive.writestr(info, data)
                archive.writestr('SOURCE-MANIFEST.json', json.dumps(manifest, indent=2) + '\n')
            with zipfile.ZipFile(temporary) as archive:
                if archive.testzip():
                    raise ValueError('Source ZIP CRC verification failed')
                if set(archive.namelist()) != set(entries) | {'SOURCE-MANIFEST.json'}:
                    raise ValueError('Source ZIP member inventory differs from the reviewed selection')
                for name, item in entries.items():
                    if hashlib.sha256(archive.read(name)).hexdigest() != item['sha256']:
                        raise ValueError('Source ZIP byte readback mismatch: ' + name)
            paused_inventory()
            temporary.rename(target)
            manifest['source_archive'] = {'sha256': sha(target), 'bytes': target.stat().st_size}
        finally:
            temporary.unlink(missing_ok=True)
    paused_inventory()
    (output / 'source-review.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': manifest['status'], 'dry_run': manifest['dry_run'], 'files': len(entries),
                      'coverage_omissions': [], 'missing_inputs': blockers, 'paused_files_verified_unchanged': len(delta),
                      'candidate_license_mismatches': candidate_mismatches,
                      'receipt': str(output / 'source-review.json')}))


if __name__ == '__main__':
    main()
