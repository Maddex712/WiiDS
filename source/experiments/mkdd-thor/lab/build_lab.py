"""Build the private Double Dash lab APK (dev.thor.mkdd.lab) without Gradle.

Inputs: the ARM64 game library from native/CMakeLists.txt (target wiids_mkdd_game) and the
Java sources in android/. Output goes to the private lab (it contains the compiled game):
never publish it. Signed with the local debug key, like the other private Thor apps.
"""
from pathlib import Path
import argparse
import shutil
import subprocess
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SDK = ROOT/'.tools/android-sdk'
TOOLS = SDK/'build-tools/34.0.0'
ANDROID_JAR = SDK/'platforms/android-34/android.jar'
STRIP = SDK/'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
JDK = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--lab', type=Path, default=Path('M:/WiiDS-mkdd-2026-11-01'))
    a = p.parse_args()
    out = a.lab/'lab-apk'
    if out.exists():
        shutil.rmtree(out)
    (out/'classes').mkdir(parents=True)
    sources = sorted(str(f) for f in (HERE.parent/'android/dev/thor/mkdd').glob('*.java'))
    run(JDK/'javac.exe', '-encoding', 'UTF-8', '-source', '8', '-target', '8', '-nowarn', '-cp', ANDROID_JAR, '-d', out/'classes', *sources)
    run(TOOLS/'d8.bat', '--min-api', '30', '--lib', ANDROID_JAR, '--output', out,
        *sorted(out.glob('classes/**/*.class')))
    run(TOOLS/'aapt2.exe', 'link', '-o', out/'base.apk', '--manifest', HERE/'AndroidManifest.xml', '-I', ANDROID_JAR)
    lib = out/'libwiids_mkdd_game.so'
    run(STRIP, '--strip-debug', '-o', lib, a.lab/'android-arm64/libwiids_mkdd_game.so')
    with zipfile.ZipFile(out/'base.apk', 'a', zipfile.ZIP_DEFLATED) as apk:
        apk.write(out/'classes.dex', 'classes.dex')
        apk.write(lib, 'lib/arm64-v8a/libwiids_mkdd_game.so')
    run(TOOLS/'zipalign.exe', '-f', '-p', '16', out/'base.apk', out/'aligned.apk')
    key = Path.home()/'.android/debug.keystore'
    run(TOOLS/'apksigner.bat', 'sign', '--ks', key, '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
        '--ks-key-alias', 'androiddebugkey', '--out', out/'DoubleDash-lab.apk', out/'aligned.apk')
    print('Built', out/'DoubleDash-lab.apk')


if __name__ == '__main__':
    main()
