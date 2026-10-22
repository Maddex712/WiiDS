"""Compare the new frontend with beta5's native libraries and bundled data."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[2]
old = ROOT / 'build/public-release-beta5/WiiDS-0.9.0-beta.5-arm64.apk'
new = ROOT / 'reference/driftdroid/android/app/build/outputs/apk/release/app-release.apk'
destination = ROOT / 'build/galaxy-thor/WiiDS-0.9.0-beta.6-galaxy-dev.apk'
def digest(stream):
    with stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()
with zipfile.ZipFile(old) as a, zipfile.ZipFile(new) as b:
    assert len(b.namelist()) == len(set(b.namelist())), 'Duplicate APK members'
    native = [n for n in a.namelist() if n.startswith('lib/') and n.endswith('.so')]
    assets = [n for n in a.namelist() if n.startswith('assets/') and not n.startswith('assets/dexopt/') and not n.endswith('/')]
    assert len(native) >= 13, 'Incomplete baseline native library set'
    assert set(native) == {n for n in b.namelist() if n.startswith('lib/') and n.endswith('.so')}
    for name in native + assets:
        assert name in b.namelist(), f'Missing entry: {name}'
        assert digest(a.open(name)) == digest(b.open(name)), f'Unexpected changed runtime/data: {name}'
    receipt = {'nativeLibrariesUnchanged': len(native), 'bundledAssetsUnchanged': len(assets),
               'nativeNames': native, 'sha256': digest(new.open('rb')), 'bytes': new.stat().st_size,
               'baseline': old.name, 'result': 'PASS'}
shutil.copyfile(new, destination)
(ROOT / 'build/galaxy-thor/wiids-package-audit.json').write_text(json.dumps(receipt, indent=2))
print(json.dumps(receipt))
