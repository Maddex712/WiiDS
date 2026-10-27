"""WiiDS 1.1.1 release APK: rumble-preview-1 plus the libadrenotools/linkernsbypass licence notices
(BSD-2-Clause; the driver loader has shipped since the GPU driver round), versioned 1.1.1 / 1111.
Audits every entry against rumble-preview-1: only version-bearing entries may change and only the
two notices may be added."""
from pathlib import Path
import hashlib, json, os, shutil, subprocess, zipfile

R = Path(__file__).resolve().parents[2]
W = Path('M:/WiiDS-rr-6.13.4')
S = W / 'android'
OUT = Path('M:/WiiDS-release-1.1.1')
PREV = W / 'WiiDS-1.1-rumble-preview-1.apk'
FINAL = OUT / 'WiiDS-1.1.1-arm64.apk'
ADRENO = Path('M:/WiiDS-drivers/libadrenotools')
NOTICES = {'libadrenotools/LICENSE-libadrenotools.txt': ADRENO / 'LICENSE',
           'libadrenotools/LICENSE-linkernsbypass.txt': ADRENO / 'lib/linkernsbypass/LICENSE'}


def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def member(z, n):
    with z.open(n) as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


OUT.mkdir(exist_ok=True)
assert not FINAL.exists()
assert sha(PREV) == json.loads((W / 'android-verification-round7.json').read_text())['apk_sha256']
lic = S / 'app/src/main/assets/licenses'
for dst, src in NOTICES.items():
    (lic / dst).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, lic / dst)
added_expected = {'assets/licenses/' + d: sha(s) for d, s in NOTICES.items()}
for e in json.loads((R / 'build/ra-casual/source-delta.json').read_text())['files']:
    assert sha(R / e['path']) == e['after_sha256'], e['path']

env = os.environ.copy()
env['JAVA_HOME'] = 'C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot'
cmd = [str(S / 'gradlew.bat'), '--init-script', str(W / 'build-output.gradle'), ':app:assembleRelease', '-PthorBuild=true',
       '-PwiidsApp=true', '-PthorRetroRewind=true', '-PwiidsVersionName=1.1.1', '-PwiidsVersionCode=1111',
       '--offline', '--no-daemon', '--max-workers=4', '--console=plain']
with (OUT / 'release-build.log').open('w', encoding='utf-8') as log:
    r = subprocess.run(cmd, cwd=S, env=env, stdout=log, stderr=subprocess.STDOUT)
assert r.returncode == 0, 'see release-build.log'

APK = W / 'gradle-build/app/outputs/apk/release/app-release.apk'
# The presentation manifest lists Retro Rewind 6.13.4's retargeted maps.json (the old entry still
# had 6.13.0's size and hash, which failed audit_release_apk.py and the app's own install check).
allowed = {'classes.dex', 'classes2.dex', 'classes3.dex', 'AndroidManifest.xml', 'resources.arsc',
           'assets/dexopt/baseline.prof', 'assets/dexopt/baseline.profm', 'assets/presentation/manifest.json'}
with zipfile.ZipFile(PREV) as a, zipfile.ZipFile(APK) as b:
    before = {n: member(a, n) for n in a.namelist()}
    after = {n: member(b, n) for n in b.namelist()}
added = set(after) - set(before)
removed = set(before) - set(after)
assert not removed, removed
assert added == set(added_expected), added
for n, h in added_expected.items():
    assert after[n] == h, n
changed = sorted(n for n in before if before[n] != after[n])
assert set(changed) <= allowed, changed
assert not any(n.startswith('lib/') for n in changed)
bt = R / '.tools/android-sdk/build-tools/34.0.0'
sig = subprocess.check_output([str(bt / 'apksigner.bat'), 'verify', '--verbose', '--print-certs', str(APK)], env=env, text=True)
assert '72eef06b956bc1c6848a09bf4956b158095736a6e81f43131cf93cb30788345f' in sig
badging = subprocess.check_output([str(bt / 'aapt.exe'), 'dump', 'badging', str(APK)], text=True)
assert "package: name='io.github.maddex712.wiids' versionCode='1111' versionName='1.1.1'" in badging
assert 'application-debuggable' not in badging
(OUT / 'badging.txt').write_text(badging)
(OUT / 'signature.txt').write_text(sig)
shutil.copy2(APK, FINAL)
native = {n: after[n] for n in sorted(after) if n.startswith('lib/')}
result = {'status': 'PASS', 'apk': str(FINAL), 'apk_sha256': sha(FINAL), 'bytes': FINAL.stat().st_size,
          'version': '1.1.1', 'versionCode': 1111, 'entries': len(after), 'changed_entries': changed,
          'added_entries': sorted(added), 'based_on': str(PREV), 'native_libraries': native}
(OUT / 'apk-verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({k: v for k, v in result.items() if k != 'native_libraries'}, indent=2))
