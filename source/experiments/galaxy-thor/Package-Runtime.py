"""Package the isolated Galaxy development runtime; never includes game data."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, help='Copy the verified APK to this new release path')
args = parser.parse_args()
SOURCE = Path(__file__).resolve().parent / 'android'
OUT = ROOT / 'build/galaxy-thor/runtime-apk'
NATIVE = ROOT / 'build/galaxy-thor/native'
SDK = ROOT / '.tools/android-sdk'
NDK = SDK / 'ndk/29.0.14206865'
JDK = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot')
BT = SDK / 'build-tools/34.0.0'
OUT.mkdir(parents=True, exist_ok=True)
for folder in ('classes', 'dex', 'libs'):
    (OUT / folder).mkdir(exist_ok=True)
env = dict(os.environ, JAVA_HOME=str(JDK))
env['PATH'] = str(JDK / 'bin') + os.pathsep + env['PATH']
def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], env=env, check=True, **kwargs)

receipt = json.loads((NATIVE.parent / 'native-build.json').read_text(encoding='utf-8-sig'))
for lib in receipt['libraries']:
    source = NATIVE / lib['name']
    if hashlib.sha256(source.read_bytes()).hexdigest() != lib['sha256']:
        raise SystemExit(f'Native receipt mismatch: {source.name}')
    run([NDK / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe',
         '--strip-unneeded', '-o', OUT / 'libs' / lib['name'], source])

run([BT / 'aapt2.exe', 'link', '--manifest', SOURCE / 'AndroidManifest.xml',
     '-I', SDK / 'platforms/android-34/android.jar', '-o', OUT / 'base.apk'])
run([JDK / 'bin/javac.exe', '-encoding', 'UTF-8', '-source', '8', '-target', '8',
     '-cp', SDK / 'platforms/android-34/android.jar', '-d', OUT / 'classes',
     *sorted(SOURCE.glob('*.java'))])
run([BT / 'd8.bat', '--min-api', '32', '--lib', SDK / 'platforms/android-34/android.jar',
     '--output', OUT / 'dex', *sorted((OUT / 'classes').rglob('*.class'))])
# The on-device importer (Build-Importer.py): libinstaller, its Python and nodtool, plus assets.
IMPORTER = ROOT / 'build/galaxy-thor/importer'
if (IMPORTER / 'libs/libinstaller.so').is_file():
    for lib in sorted((IMPORTER / 'libs').glob('*.so')):
        shutil.copy2(lib, OUT / 'libs' / lib.name)
with zipfile.ZipFile(OUT / 'base.apk', 'a', compression=zipfile.ZIP_DEFLATED) as apk:
    apk.write(OUT / 'dex/classes.dex', 'classes.dex')
    for asset in ('pythonhome.zip', 'cooker-scripts.zip'):
        if (IMPORTER / asset).is_file():
            apk.write(IMPORTER / asset, 'assets/' + asset, compress_type=zipfile.ZIP_STORED)
    for lib in sorted((OUT / 'libs').glob('*.so')):
        apk.write(lib, 'lib/arm64-v8a/' + lib.name)
    wrap = ROOT / 'reference/galaxy-android/platform/android/wrap.sh'
    apk.writestr('lib/arm64-v8a/wrap.sh', wrap.read_bytes().replace(b'\r\n', b'\n'))
    manifest = ROOT / 'reference/galaxy-android/assets/galaxy_shader_manifest.bin'
    if manifest.is_file():
        apk.write(manifest, 'assets/galaxy_shader_manifest.bin')
    for name, source in {
        'LICENSE-Galaxy.txt': ROOT / 'reference/galaxy-android/LICENSE',
        'LICENSE-Petari.txt': ROOT / 'reference/galaxy-android/decomp/LICENSE-Petari.txt',
        'THIRD_PARTY_NOTICES-Galaxy.md': ROOT / 'reference/galaxy-android/THIRD_PARTY_NOTICES.md',
        'WIIDS-NOTICE.md': ROOT / 'release/licenses/WIIDS-NOTICE.md',
    }.items():
        apk.write(source, 'assets/licenses/' + name)
run([BT / 'zipalign.exe', '-f', '-p', '4', OUT / 'base.apk', OUT / 'aligned.apk'])
apk = OUT / 'WiiDS-Galaxy-Thor-dev.apk'
run([BT / 'apksigner.bat', 'sign', '--ks', Path.home() / '.android/debug.keystore',
     '--ks-pass', 'pass:android', '--out', apk, OUT / 'aligned.apk'])
run([BT / 'apksigner.bat', 'verify', apk])
metadata = {'package': 'io.github.maddex712.wiids.galaxy', 'debuggable': True,
            'reason': 'Required low-address preload before ART; development runtime only',
            'gameDataBundled': False, 'bytes': apk.stat().st_size,
            'sha256': hashlib.sha256(apk.read_bytes()).hexdigest(), 'native': receipt}
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        raise SystemExit('Refusing to overwrite release output: ' + str(args.output))
    shutil.copy2(apk, args.output)
    args.output.with_suffix('.apk.packaging.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
(OUT / 'package.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
print(apk)
