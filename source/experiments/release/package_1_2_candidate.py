"""Review an existing private 1.2 APK and stage reproducible text notices.

Does not build, sign, install, upload, publish, or change the supplied APK.
Only --stage-notices copies reviewed text notices into the selected private Android tree.
Use export_1_2_source.py separately for source inventory and optional source-only ZIP.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PRIVATE = Path('M:/WiiDS-release-wrap-2026-11-08')
EXPECTED = '49c54110846b9d2f056ae2ad1973ee16c923da0e39784f301ba2c5871c0e9cdc'
OWNER = '72eef06b956bc1c6848a09bf4956b158095736a6e81f43131cf93cb30788345f'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--private-root', type=Path, default=PRIVATE)
    p.add_argument('--apk', type=Path)
    p.add_argument('--expected-apk-sha256', default=EXPECTED)
    p.add_argument('--expected-version-code', type=int)
    p.add_argument('--stage-notices', action='store_true', help='Copy the reviewed text notices into the selected private Android tree; APK unchanged')
    a = p.parse_args()
    private = a.private_root.resolve()
    apk = a.apk or private / 'WiiDS-1.2-candidate3.apk'
    if sha(apk) != a.expected_apk_sha256:
        raise ValueError('Candidate APK differs from the explicitly reviewed SHA-256')
    tools = ROOT / '.tools/android-sdk/build-tools/34.0.0'
    badging = subprocess.check_output([str(tools / 'aapt.exe'), 'dump', 'badging', str(apk)], text=True)
    certificate = subprocess.check_output([str(tools / 'apksigner.bat'), 'verify', '--print-certs', str(apk)], text=True)
    if OWNER not in certificate or 'application-debuggable' in badging:
        raise ValueError('Candidate must be owner-signed and nondebuggable')
    package = re.search(r"package: name='([^']+)' versionCode='(\d+)' versionName='([^']+)'", badging)
    if not package or package.group(1) != 'io.github.maddex712.wiids' or package.group(3) != '1.2':
        raise ValueError('Candidate package/version must be WiiDS 1.2')
    version_code = int(package.group(2))
    if a.expected_version_code is not None and version_code != a.expected_version_code:
        raise ValueError('Unexpected versionCode')
    destination = private / 'package-review'
    notices = destination / 'licenses'
    notices.mkdir(parents=True, exist_ok=True)
    sources = {
        'LICENSE-MIT-gcn-recomp.txt': Path('M:/WiiDS-mkdd-2026-11-01/runtime/gcn-recomp/LICENSE'),
        'LICENSE-native-texture-decoders.txt': private / 'android/app/src/main/assets/licenses/LICENSE-native-texture-decoders.txt',
        'THIRD-PARTY-NOTICES-Brawl.md': Path('M:/WiiDS-brawl/src/wiicompiled/THIRD-PARTY-NOTICES.md'),
        'LICENSE-GPL-Brawl.txt': Path('M:/WiiDS-brawl/src/wiicompiled/LICENSE'),
        'LICENSE-MIT-Brawl-aurora.txt': Path('M:/WiiDS-brawl/src/wiicompiled/aurora-main/LICENSE'),
        'libarchive-3.8.11.txt': Path('M:/WiiDS-mkdd-2026-11-01/all-game-packs-2026-11-04/deps/libarchive-3.8.11/COPYING'),
        'xz-5.8.4.txt': Path('M:/WiiDS-mkdd-2026-11-01/all-game-packs-2026-11-04/deps/xz-5.8.4/COPYING'),
        'LICENSE-XZ-0BSD.txt': Path('M:/WiiDS-mkdd-2026-11-01/all-game-packs-2026-11-04/deps/xz-5.8.4/COPYING.0BSD'),
    }
    # Individual runtime dependencies retain their controlling notices, beyond the summary.
    base = Path('M:/WiiDS-brawl/src/wiicompiled/runtime/third_party')
    for component, name in [('toml11', 'LICENSE'), ('pugixml', 'LICENSE.md'), ('libco', 'LICENSE'), ('fatfs', 'LICENSE.txt'), ('cryptopp', 'License.txt')]:
        sources['LICENSE-Brawl-' + component + '.txt'] = base / component / name
    staged, missing = {}, []
    for name, source in sources.items():
        if not source.is_file():
            missing.append(name)
            continue
        raw = source.read_bytes()
        if b'\0' in raw:
            raise ValueError('Binary notice refused: ' + name)
        (notices / name).write_bytes(raw)
        staged[name] = hashlib.sha256(raw).hexdigest()
    with zipfile.ZipFile(apk) as z:
        names = z.namelist()
        native = {n: hashlib.sha256(z.read(n)).hexdigest() for n in names if n.startswith('lib/') and n.endswith('.so')}
        dex = {n: hashlib.sha256(z.read(n)).hexdigest() for n in names if re.fullmatch(r'classes\d*\.dex', n)}
        needs_staging = [name for name, digest in staged.items() if 'assets/licenses/' + name not in names or
                         hashlib.sha256(z.read('assets/licenses/' + name)).hexdigest() != digest]
    changed_android = []
    if a.stage_notices:
        android_notices = private / 'android/app/src/main/assets/licenses'
        for name, digest in staged.items():
            target = android_notices / name
            if target.is_file() and sha(target) == digest:
                continue
            if target.exists():
                previous = destination / 'license-before' / (name + '.' + sha(target))
                previous.parent.mkdir(parents=True, exist_ok=True)
                if not previous.exists():
                    previous.write_bytes(target.read_bytes())
            target.write_bytes((notices / name).read_bytes())
            if sha(target) != digest:
                raise ValueError('Staged notice readback mismatch: ' + name)
            changed_android.append(str(target))
    if len(native) != 36:
        raise ValueError('Native inventory changed; review the expected 36-library profile before proceeding')
    android_notices = private / 'android/app/src/main/assets/licenses'
    needs_android = [name for name, digest in staged.items() if not (android_notices / name).is_file() or sha(android_notices / name) != digest]
    result = {'status': 'REVIEW_REQUIRED', 'version_name': '1.2', 'version_code': version_code,
              'apk_sha256': a.expected_apk_sha256, 'apk_bytes': apk.stat().st_size,
              'owner_signed': True, 'nondebuggable': True, 'native_libraries': native, 'dex': dex,
              'staged_notice_hashes': staged, 'missing_notice_sources': missing,
              'notices_to_reconcile_with_android': needs_android,
              'notices_missing_from_candidate': needs_staging,
              'changed_android_notice_paths': changed_android,
              'limitations': ['Existing candidate inspected only; no fresh build or device acceptance.',
                              'Source/native/DEX correspondence and Nintendo-byte boundaries require final audit.',
                              'Notices enter private Android only with --stage-notices; the APK is never changed.'],
              'built': False, 'installed': False, 'published': False}
    (destination / 'candidate-review.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    (destination / 'badging.txt').write_text(badging, encoding='utf-8')
    (destination / 'certificate.txt').write_text(certificate, encoding='utf-8')
    print(json.dumps({'status': result['status'], 'version_code': version_code, 'native_libraries': len(native),
                      'missing_notice_sources': missing, 'notices_to_reconcile_with_android': needs_android,
                      'notices_missing_from_candidate': needs_staging,
                      'changed_android_notice_paths': changed_android,
                      'receipt': str(destination / 'candidate-review.json')}))


if __name__ == '__main__':
    main()
