"""Stage the other games' native libraries and runtime assets into the one WiiDS APK.

The Mario Kart app (reference/driftdroid) is the host. This copies, strips and checks:
  libnsmbw.so, libmsmbwii.so   NSMB Wii and MSMBWii products (MKW_ANDROID_LIBRARY_NAME builds)
  libSDL2.so                   Sunshine's SDL 2 with its Java glue in org.libsdl.sdl2.app
  libsms.so, libsmsbootstrap.so  Super Mario Sunshine
and the NSMB runtime asset trees as assets/runtime-nsmbw and assets/runtime-msmbwii,
plus the MSMBWii mod (assets/mods/MSMBWii.zip, included with its authors' permission).
libc++_shared.so and libpng16.so come from the host build (checked compatible).
Run before scripts/Package-Thor.ps1. Never stages game data.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOST = ROOT / 'reference/driftdroid/android/app'
JNI = HOST / 'src/main/jniLibs/arm64-v8a'
ASSETS = ROOT / 'build/wiids-merge/assets'
NDK = ROOT / '.tools/android-sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin'

LIBRARIES = {
    'libnsmbw.so': ROOT / 'build/nsmbw-thor/e1/android-native/libnsmbw.so',
    'libmsmbwii.so': ROOT / 'build/nsmbw-thor/msmbwii/android-native/libmsmbwii.so',
    'libSDL2.so': ROOT / 'build/sunshine-thor/android-native-wiids/SDL2/libSDL2.so',
    'libsms.so': ROOT / 'build/sunshine-thor/jniLibs/arm64-v8a/libsms.so',
    'libsmsbootstrap.so': ROOT / 'build/sunshine-thor/jniLibs/arm64-v8a/libsmsbootstrap.so',
}
MSMBWII_SHA256 = 'be135baea3a6caeb6510574e5bf0ed55a6e3fdfbe51d191195134d865faa30cf'
RUNTIME_ASSETS = {
    'runtime-nsmbw': ROOT / 'reference/nsmbw-android-e1/android/native/assets/runtime',
    'runtime-msmbwii': ROOT / 'reference/nsmbw-android-msmbwii/android/native/assets/runtime',
}


def run(*args):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    if result.returncode:
        sys.exit(f'{args[0]} failed: {result.stderr.strip()}')
    return result.stdout


def replace_runtime_assets(name, source):
    """Replace this stager's named runtime tree, preserving other release assets."""
    if name not in RUNTIME_ASSETS:
        raise ValueError('Unknown runtime asset tree')
    ASSETS.mkdir(parents=True, exist_ok=True)
    root = ASSETS.resolve()
    if not root.is_relative_to((ROOT / 'build').resolve()) or not root.is_relative_to(ROOT.resolve()):
        raise ValueError('Asset staging root escapes the workspace build directory')
    token = uuid.uuid4().hex
    target = ASSETS / name
    stage = ASSETS / f'.{name}.{token}.new'
    backup = ASSETS / f'.{name}.{token}.old'

    def checked(path):
        if path not in (target, stage, backup) or path.parent.resolve() != root:
            raise ValueError('Unexpected runtime staging path')
        if path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction()):
            raise ValueError('Runtime asset staging may not follow a symlink/junction')
        if not path.resolve().is_relative_to(root):
            raise ValueError('Runtime staging target escapes its asset root')

    def cleanup(path):
        checked(path)
        if path.exists():
            shutil.rmtree(path)

    for path in (target, stage, backup):
        checked(path)
    try:
        shutil.copytree(source, stage)
        if target.exists():
            os.replace(target, backup)
        try:
            os.replace(stage, target)
        except BaseException:
            if backup.exists():
                os.replace(backup, target)
            raise
        cleanup(backup)
    finally:
        cleanup(stage)


def main():
    JNI.mkdir(parents=True, exist_ok=True)
    for name, source in LIBRARIES.items():
        if not source.is_file():
            sys.exit(f'Missing native output: {source}')
        target = JNI / name
        shutil.copyfile(source, target)
        run(NDK / 'llvm-strip.exe', '--strip-unneeded', target)
        dynamic = run(NDK / 'llvm-readelf.exe', '-d', '-l', target)
        if f'[{name}]' not in dynamic and name not in ('libsms.so', 'libsmsbootstrap.so'):
            sys.exit(f'{name}: unexpected SONAME')
        if 'LOAD' in dynamic and '0x4000' not in dynamic:
            print(f'warning: {name} is not 16 KB page aligned')
        print(f'{name}: {target.stat().st_size / 1048576:.1f} MB')
    exports = run(NDK / 'llvm-nm.exe', '-D', '--defined-only', JNI / 'libSDL2.so')
    if 'Java_org_libsdl_app_' in exports or 'Java_org_libsdl_sdl2_app_' not in exports:
        sys.exit('libSDL2.so is not the relocated WiiDS build')
    for name, source in RUNTIME_ASSETS.items():
        replace_runtime_assets(name, source)
        print(f'assets/{name}: {sum(1 for p in (ASSETS / name).rglob("*") if p.is_file())} files')
    # MSMBWii is included with its authors' permission (docs/release/PERMISSIONS.md). Only the
    # exact known release is staged; the app checks it again before unpacking.
    mod = ROOT / 'build/mods/msmbwii/download/MSMBWii.zip'
    data = mod.read_bytes()
    if hashlib.sha256(data).hexdigest() != MSMBWII_SHA256:
        sys.exit('MSMBWii.zip is not the known release')
    (ASSETS / 'mods').mkdir(parents=True, exist_ok=True)
    (ASSETS / 'mods/MSMBWii.zip').write_bytes(data)
    print('assets/mods/MSMBWii.zip: bundled')


if __name__ == '__main__':
    main()
