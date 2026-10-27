"""WiiDS 1.1.1 public assets: source ZIP (the public 1.1 ZIP with exactly the changed and new
source swapped in), RELEASE-INVENTORY.json, UI-ASSET-MANIFEST.json, SHA256SUMS.txt, and the
public checkout (source mirror, docs/release/V1.1.1.md, README). Nothing is committed, pushed or
uploaded here. Paused achievements sources, staged assets and machine-local files never enter."""
from pathlib import Path
import copy, hashlib, json, shutil, subprocess, zipfile

R = Path(__file__).resolve().parents[2]
B = Path(__file__).resolve().parent
W = Path('M:/WiiDS-rr-6.13.4')
OUT = Path('M:/WiiDS-release-1.1.1')
OLD = Path('M:/WiiDS-updater-1.1-2026-10-25/WiiDS-1.1-source.zip')
OLD_SHA = '85599f23914cf5275c1a2f5697256216c8b2d510c7231ba2d4cf235906a6d600'
NEW = OUT / 'WiiDS-1.1.1-source.zip'
APK = OUT / 'WiiDS-1.1.1-arm64.apk'
C = R / 'build/github-publication/WiiDS'
BF = Path('M:/WiiDS-drivers/bloodframe')
AD = Path('M:/WiiDS-drivers/libadrenotools')
SOURCE_EXT = {'.py', '.kt', '.java', '.c', '.cpp', '.h', '.hpp', '.ps1'}

sha = lambda data: hashlib.sha256(data).hexdigest()


def fsha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


# zip prefix -> (working tree, new files allowed, files that must stay out)
paused = {e['path'].replace('\\', '/') for e in json.loads((R / 'build/ra-casual/source-delta.json').read_text())['files']}
ROOTS = {
    'reference/driftdroid/android': (W / 'android', lambda rel: rel.startswith(('app/src/main/java/', 'app/src/main/kotlin/'))),
    'reference/driftdroid/runtime': (W / 'driftdroid/runtime', lambda rel: rel == 'src/android_vulkan_override.cpp'),
    'reference/driftdroid/aurora-main': (W / 'driftdroid/aurora-main', lambda rel: rel in ('lib/webgpu/lsfg.cpp', 'lib/webgpu/lsfg.hpp')),
    'reference/nsmbw-android-e1': (R / 'reference/nsmbw-android-e1', lambda rel: False),
    'reference/nsmbw-android-msmbwii': (R / 'reference/nsmbw-android-msmbwii', lambda rel: False),
    'reference/galaxy-android/platform': (R / 'reference/galaxy-android/platform', lambda rel: False),
    'third_party/SDL2-wiids': (R / 'build/sunshine-thor/deps/SDL2-2.32.10-wiids', lambda rel: False),
}
SKIP_DIRS = {'build', '.gradle', '.git', '.cxx', 'generated', '__pycache__', 'jniLibs', '.idea', 'gradle-build', 'assets'}

with OLD.open('rb') as f:
    assert hashlib.file_digest(f, 'sha256').hexdigest() == OLD_SHA
assert APK.is_file()
old = zipfile.ZipFile(OLD)
original = set(old.namelist())
replacement = {}
for prefix, (tree, allow_new) in ROOTS.items():
    zipped = {n[len(prefix) + 1:] for n in original if n.startswith(prefix + '/')}
    for rel in sorted(zipped):
        p = tree / rel
        if p.is_file() and p.read_bytes() != old.read(prefix + '/' + rel):
            replacement[prefix + '/' + rel] = p.read_bytes()
    for p in tree.rglob('*'):
        if p.is_file() and not any(part in SKIP_DIRS for part in p.relative_to(tree).parts[:-1]):
            rel = p.relative_to(tree).as_posix()
            if rel not in zipped and allow_new(rel):
                replacement[prefix + '/' + rel] = p.read_bytes()
# The working tree's build.gradle.kts is the private packaging shortcut (prebuilt native libraries,
# no externalNativeBuild or shared asset folders); the public build keeps 1.1's.
WORKBENCH_ONLY = {'reference/driftdroid/android/app/build.gradle.kts'}
for name in [n for n in replacement if n in paused or n in WORKBENCH_ONLY]:
    del replacement[name]  # paused achievements work / workbench packaging: the public 1.1 version stays
assert not any(n in paused for n in replacement)
assert not any('thor_ra' in n or 'achievements' in n.lower() for n in replacement), 'paused client leaked'

# Discord round's GameCube import validation (the shipped libwiidsdisc.so was built from this).
disc = R / 'build/discord-bugfix-2026-10-26/rom-import/native-source/wiids/disc/src/wiids_disc.cpp'
replacement['wiids/disc/src/wiids_disc.cpp'] = disc.read_bytes()
replacement['release/version.properties'] = (b'# One version for every WiiDS app. Packaging scripts pass these to Gradle.\n'
                                             b'wiidsVersionName=1.1.1\nwiidsVersionCode=1111\n')
replacement['docs/release/V1.1.1.md'] = (OUT / 'release-notes.md').read_bytes()

# Third-party libraries built outside this tree: provenance, licences and WiiDS's patch.
bf_patch = subprocess.check_output(['git', '-C', str(BF), 'diff', '--', 'framegen/src/core/device.cpp'])
assert b'ahbAllocatorAcceptsStorage' in bf_patch
replacement['third_party/bloodframe/wiids-ahb-storage-fallback.patch'] = bf_patch
replacement['third_party/bloodframe/LICENSE.md'] = (BF / 'LICENSE.md').read_bytes()
replacement['third_party/bloodframe/build_bloodframe.ps1'] = (B / 'build_bloodframe.ps1').read_bytes()
replacement['third_party/bloodframe/PROVENANCE.json'] = (json.dumps({
    'upstream': 'https://github.com/ducvd89/bloodframe', 'commit': subprocess.check_output(['git', '-C', str(BF), 'rev-parse', 'HEAD'], text=True).strip(),
    'license': 'MIT (bloodframe, framegen); dxbc, pe-parse and volk under their own licences (bundled in the APK under assets/licenses/bloodframe)',
    'wiids_patch': 'wiids-ahb-storage-fallback.patch: test-allocate a GPU_DATA_BUFFER AHardwareBuffer before choosing direct-storage transport; gralloc 4.0 (AYN Thor) refuses it, so Bloodframe uses direct-input-copy-output.',
    'build': 'build_bloodframe.ps1 (NDK 29, arm64-v8a, android-30, c++_shared, Release, stripped)',
    'loaded_at_runtime': 'libbloodframe.so is opened with dlopen only when Lossless Scaling is selected; it reads the player\'s own Lossless.dll, which is not distributed.',
}, indent=2) + '\n').encode()
replacement['third_party/libadrenotools/LICENSE'] = (AD / 'LICENSE').read_bytes()
replacement['third_party/libadrenotools/LICENSE-linkernsbypass'] = (AD / 'lib/linkernsbypass/LICENSE').read_bytes()
replacement['third_party/libadrenotools/PROVENANCE.json'] = (json.dumps({
    'upstream': 'https://github.com/bylaws/libadrenotools',
    'commit': subprocess.check_output(['git', '-C', str(AD), 'rev-parse', 'HEAD'], text=True).strip(),
    'linkernsbypass_commit': subprocess.check_output(['git', '-C', str(AD / 'lib/linkernsbypass'), 'rev-parse', 'HEAD'], text=True).strip(),
    'license': 'BSD-2-Clause (both)',
    'use': 'MKW_ANDROID_CUSTOM_GPU_DRIVERS=ON builds the four hook libraries and links the loader into libWiiCompiled.so/libRetroRewind.so.',
    'dawn': 'Dawn\'s DynamicLib dlopen/dlclose are renamed to wiids_dawn_dlopen/wiids_dawn_dlclose with llvm-objcopy --redefine-sym (see experiments/release/1.1.1/build_native_drivers.py) so a custom Vulkan driver can be substituted without wrapping dlopen, which linkernsbypass inspects.',
}, indent=2) + '\n').encode()
for script in ('build_native_drivers.py', 'build_nsmbw_rumble.py', 'build_release_1_1_1.py', 'prepare_release_1_1_1.py'):
    replacement['experiments/release/1.1.1/' + script] = (B / script).read_bytes()
replacement['experiments/release/1.1.1/README.md'] = b'''# WiiDS 1.1.1 native builds

Recorded build steps for the libraries that changed in 1.1.1. Paths refer to the maintainer's
workbench; the steps and options are what matter.

- `libWiiCompiled.so`, `libRetroRewind.so`: `build_native_drivers.py` (custom GPU drivers, renamed
  Dawn, Lossless Scaling, rumble glue). Retro Rewind 6.13.4 is translated from the player-independent
  mod payload; translated game code is not published.
- `libnsmbw.so`, `libmsmbwii.so`: `build_nsmbw_rumble.py` reproduces the shipped libraries from the
  accepted link first, then recompiles only the unit holding `hle/input/wpad.cpp`.
- `libgame.so`: Galaxy's ninja build after `platform/mobile/core/MobileApp.cpp` (sustained rumble).
- `libSDL2.so`: `third_party/SDL2-wiids` with Android joystick rumble (`SDL_android.c/.h`,
  `joystick/android/SDL_sysjoystick.c`).
- `libbloodframe.so`: `third_party/bloodframe/build_bloodframe.ps1` with the WiiDS patch.
- `libwiidsdisc.so`: `wiids/disc` (GameCube import validation).
- Release APK: `build_release_1_1_1.py`; release assets: `prepare_release_1_1_1.py`.
'''

with old:
    manifest = json.loads(old.read('SOURCE-MANIFEST.json'))
    roots = manifest['source_coverage']['roots']
    for name, data in replacement.items():
        manifest['files'][name] = {'sha256': sha(data), 'bytes': len(data)}
    added = sorted(n for n in replacement if n not in original)
    counted = []
    for name in added:
        if Path(name).suffix.lower() not in SOURCE_EXT:
            continue
        root = next((r for r in sorted(roots, key=len, reverse=True) if name.startswith(r + '/')), None)
        if root is None:
            root = '/'.join(name.split('/')[:2])
            roots[root] = 0
        roots[root] += 1
        manifest['source_coverage']['expected_source_files'] += 1
        counted.append(name)
    manifest['release_1_1_1'] = {
        'base_source_sha256': OLD_SHA, 'version': '1.1.1', 'versionCode': 1111,
        'changed': sorted(n for n in replacement if n in original), 'added': added, 'counted_source_files': counted,
        'excluded': ['paused achievements client (reference/nsmbw-android-e1 thor_ra.*, vi.cpp, CMake product lists)',
                     'staged APK assets and licence copies (bundled presentation stays outside the source archive)',
                     'translated guest code (private, derived from the game)', 'machine-local Gradle files'],
    }
    replacement['SOURCE-MANIFEST.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    with zipfile.ZipFile(NEW, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as new:
        for info in old.infolist():
            if info.filename in replacement:
                new.writestr(copy.copy(info), replacement[info.filename])
            else:
                with old.open(info) as src, new.open(copy.copy(info), 'w') as dst:
                    shutil.copyfileobj(src, dst, 1024 * 1024)
        for name in sorted(replacement.keys() - original):
            new.writestr(name, replacement[name])

with zipfile.ZipFile(OLD) as a, zipfile.ZipFile(NEW) as b:
    names = b.namelist()
    assert len(names) == len(set(names))
    for name in names:
        data = b.read(name)
        if name in replacement:
            assert data == replacement[name], name
        else:
            assert name in original and data == a.read(name), name
        if name in manifest['files']:
            assert manifest['files'][name] == {'sha256': sha(data), 'bytes': len(data)}, name
source_sha, source_bytes = fsha(NEW), NEW.stat().st_size

# Release metadata read by the in-app updater (WiiDsUpdateRelease.bindInventory).
audit = json.loads((OUT / 'APK-AUDIT.json').read_text())
apkv = json.loads((OUT / 'apk-verification.json').read_text())
assert audit['status'] == 'pass' and not audit['findings'] and audit['apk_sha256'] == fsha(APK) == apkv['apk_sha256']
with zipfile.ZipFile(APK) as z:
    ui = z.read('assets/presentation/manifest.json')
assert sha(ui) == audit['approved_presentation']['manifest_sha256']
(OUT / 'UI-ASSET-MANIFEST.json').write_bytes(ui)
inventory = json.loads(Path('M:/WiiDS-updater-1.1-2026-10-25/RELEASE-INVENTORY.json').read_text())
for key in ('hotfix', 'updater', 'git_tree', 'source_commit'):
    inventory.pop(key, None)
inventory.update({
    'tag': 'v1.1.1', 'version': '1.1.1', 'versionCode': 1111, 'debuggable': False, 'single_apk': True,
    'package': 'io.github.maddex712.wiids',
    'signer_certificate_sha256': '72eef06b956bc1c6848a09bf4956b158095736a6e81f43131cf93cb30788345f',
    'private_roms_cooked_game_data_saves_credentials_signing_keys_included': False,
    'artifacts': {APK.name: {'bytes': APK.stat().st_size, 'sha256': fsha(APK)},
                  NEW.name: {'bytes': source_bytes, 'sha256': source_sha}},
    'native_libraries': audit['native_libraries'],
    'approved_mod_payloads': audit['approved_mod_payloads'],
    'approved_presentation': audit['approved_presentation'],
    'native_source_window_checks': len(audit['source_window_checks']),
    'source': {'archive_entries': len(names), 'expected_source_files': manifest['source_coverage']['expected_source_files'],
               'coverage_omissions': len(manifest['source_coverage']['omissions']), 'all_manifest_bytes_verified': True,
               'paused_earning_client_excluded': True, 'clean_machine_rebuild_claimed': False,
               'changed_from_1_1': len(manifest['release_1_1_1']['changed']), 'added_since_1_1': len(added)},
    'validation': {'based_on_preview': apkv['based_on'], 'changed_release_entries': apkv['changed_entries'],
                   'added_release_entries': apkv['added_entries'], 'installed_on_thor_before_publication': True,
                   'not_hardware_tested': ['Retro WFC online', 'Lossless Scaling in a race', 'rumble felt in gameplay', 'NSMB tilt direction']},
    'third_party_runtime': {'libbloodframe.so': 'MIT, see third_party/bloodframe', 'libadrenotools hooks': 'BSD-2-Clause, see third_party/libadrenotools',
                            'Lossless.dll': 'not distributed; imported by the player'},
})
inventory_bytes = (json.dumps(inventory, indent=2) + '\n').encode()
(OUT / 'RELEASE-INVENTORY.json').write_bytes(inventory_bytes)
order = [APK.name, NEW.name, 'APK-AUDIT.json', 'UI-ASSET-MANIFEST.json', 'RELEASE-INVENTORY.json']
(OUT / 'SHA256SUMS.txt').write_text(''.join(f'{fsha(OUT / n)}  {n}\n' for n in order), newline='\n')

# Public checkout: source mirror (presentation payloads stay out, as in 1.1), notes and README.
assert not subprocess.check_output(['git', '-C', str(C), 'status', '--porcelain'])
for name, data in replacement.items():
    if name.startswith('build/wiids-merge/assets/presentation/') and name != 'build/wiids-merge/assets/presentation/manifest.json':
        continue
    p = C / 'source' / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
(C / 'docs/release/V1.1.1.md').write_bytes((OUT / 'release-notes.md').read_bytes())
readme = C / 'README.md'
text = readme.read_text(encoding='utf-8')
for bad, good in {'\u00e2\u2020\u2019': '\u2192', '\u00e2\u20ac\u2122': '\u2019', '\u00c2\u00b7': '\u00b7',
                  '\u00e2\u20ac\u201c': '\u2013', '\u00e2\u20ac\u201d': '\u2014'}.items():
    text = text.replace(bad, good)
old_update = next(line for line in text.splitlines() if line.startswith('**1.1 update:**'))
text = text.replace(old_update, '**1.1.1:** Retro Rewind 6.13.4 with online play, Lossless Scaling frame generation, rumble, custom GPU '
                    'drivers and a Wii Menu theme. Already on 1.1? Open **Options \u2192 Check for updates**, or install this APK over '
                    'your existing copy (do not uninstall). [Full 1.1.1 patch notes](docs/release/V1.1.1.md).', 1)
old_download = '**[Download WiiDS 1.1 for AYN Thor (APK, 1.85 GB)](https://github.com/Maddex712/WiiDS/releases/download/v1.1.0/WiiDS-1.1-arm64.apk)**'
assert text.count(old_download) == 1
text = text.replace(old_download, '**[Download WiiDS 1.1.1 for AYN Thor (APK, 1.85 GB)](https://github.com/Maddex712/WiiDS/releases/download/v1.1.1/WiiDS-1.1.1-arm64.apk)**')
assert text.count('[Full 1.1 patch notes](docs/release/V1.1.md)') == 1
text = text.replace('[Full 1.1 patch notes](docs/release/V1.1.md)', '[Full 1.1.1 patch notes](docs/release/V1.1.1.md) \u00b7 [1.1 patch notes](docs/release/V1.1.md)')
assert text.count('## New in 1.1\n') == 1
text = text.replace('## New in 1.1\n', '''## New in 1.1.1

- **Retro Rewind 6.13.4** with Retro WFC online, and an updater that steps through releases WiiDS can't run.
- **Lossless Scaling frame generation** for Mario Kart Wii and Retro Rewind, with your own `Lossless.dll` (not included).
- **Rumble** in every game, on the Thor's motor or a controller's own. Android's Vibration setting must be on.
- **Custom GPU drivers** for Mario Kart Wii and Retro Rewind, and a **Wii Menu theme**.
- Fixes for NSMB tilt and world unlocks, GameCube import, microSD imports, Galaxy movies, spin and Quick Save.

## New in 1.1
''', 1)
readme.write_text(text, encoding='utf-8', newline='\n')

result = {'status': 'PREPARED', 'source_zip': str(NEW), 'source_sha256': source_sha, 'source_bytes': source_bytes,
          'archive_entries': len(names), 'changed_from_1_1': manifest['release_1_1_1']['changed'], 'added_since_1_1': added,
          'assets': {n: {'sha256': fsha(OUT / n), 'bytes': (OUT / n).stat().st_size} for n in order + ['SHA256SUMS.txt']},
          'checkout_changes': subprocess.check_output(['git', '-C', str(C), 'status', '--porcelain'], text=True).splitlines()}
(OUT / 'prepared.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({k: (len(v) if isinstance(v, list) else v) for k, v in result.items() if k != 'assets'}, indent=2))
