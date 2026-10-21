"""Stage Eclipse's existing host/runtime into WiiDS, without touching any user profile.

The launcher host is relocated to its own Java namespace and Android processes.
Only its small bootstrap is rebuilt. The Eclipse game library is copied verbatim;
the main app's relocated SDL2 is reused. No disc, save or extracted HUD is staged.
"""
from pathlib import Path
import hashlib
import argparse
import os
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'reference/eclipse-launcher-thor/android/app/src/main/java'
APP = ROOT / 'reference/driftdroid/android/app/src/main'
JAVA = APP / 'java/dev/chasem/eclipselauncher'
JNI = APP / 'jniLibs/arm64-v8a'
OUT = ROOT / 'build/eclipse-thor/embedded'


def sha(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()


def run(*args):
    result = subprocess.run([str(a) for a in args], text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle-patch', action='store_true', help='Stage the verified official Eclipse patch as an APK asset; never a patched disc')
    parser.add_argument('--sdk-root', type=Path, default=Path(os.environ.get('ANDROID_SDK_ROOT') or os.environ.get('ANDROID_HOME') or ROOT / '.tools/android-sdk'))
    args = parser.parse_args()
    NDK = args.sdk_root / 'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64'
    JAVA.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    sources = {}
    for source in sorted((SOURCE / 'dev/chasem/smslauncher').glob('*.java')):
        text = source.read_text(encoding='utf-8').replace('dev.chasem.smslauncher', 'dev.chasem.eclipselauncher')
        text = text.replace('org.libsdl.app', 'org.libsdl.sdl2.app')
        text = text.replace('getSharedPreferences("settings",', 'getSharedPreferences("eclipse_settings",')
        text = text.replace('getSharedPreferences("controller-profiles",', 'getSharedPreferences("eclipse_controller-profiles",')
        text = text.replace('getSharedPreferences("transport",', 'getSharedPreferences("eclipse_transport",')
        text = text.replace('"SDL2", "smsbootstrap"', '"SDL2", "eclipsebootstrap"')
        text = text.replace('getPackageName() + ":game"', 'getPackageName() + ":eclipse_game"')
        text = text.replace('eclipse = activity.getPackageName().contains("eclipse");', 'eclipse = true;')
        if source.name == 'ThorStorage.java':
            text = text.replace('File root = context.getExternalFilesDir(null);', 'File external = context.getExternalFilesDir(null);\n        File root = external == null ? null : new File(external, "eclipse");')
            text = text.replace('return root;', 'root.mkdirs();\n        return root;', 1)
            text = text.replace('new File(root(context), "disc/game.iso")', 'disc(context)')
            text = text.replace('public static void validateDisc(Context context)', 'public static File disc(Context context) {\n        return new File(context.getFilesDir(), "games/eclipse/game.iso");\n    }\n    public static void validateDisc(Context context)')
            # An integrated launch always returns to this package. Keep the old setter as a
            # harmless compatibility method for the versioned entry transport.
            text = text.replace('context.getSharedPreferences("eclipse_transport", Context.MODE_PRIVATE).getString("library", "io.github.maddex712.wiids")', 'context.getPackageName()')
        text = text.replace('new File(root, "disc/game.iso")', 'ThorStorage.disc(this)')
        text = text.replace('new File(ThorStorage.root(this), "disc/game.iso")', 'ThorStorage.disc(this)')
        (JAVA / source.name).write_text(text, encoding='utf-8', newline='\n')
        sources[source.name] = sha(source)
    config = (APP / 'java/dev/chasem/smslauncher/BuildConfig.java').read_text(encoding='utf-8')
    (JAVA / 'BuildConfig.java').write_text(config.replace('dev.chasem.smslauncher', 'dev.chasem.eclipselauncher'), encoding='utf-8', newline='\n')
    entry = (SOURCE / 'com/wither/sms/ThorLaunchActivity.java').read_text(encoding='utf-8')
    entry = entry.replace('package com.wither.sms;', 'package dev.chasem.eclipselauncher;')
    entry = entry.replace('dev.chasem.smslauncher', 'dev.chasem.eclipselauncher')
    (JAVA / 'ThorLaunchActivity.java').write_text(entry, encoding='utf-8', newline='\n')

    # Shared SDL2 already has WiiDS's org.libsdl.sdl2.app JNI names. Eclipse's original
    # SDL2 would collide with that filename and has incompatible Java class names.
    exports = run(NDK / 'bin/llvm-nm.exe', '-D', '--defined-only', JNI / 'libSDL2.so')
    if 'Java_org_libsdl_sdl2_app_' not in exports:
        raise ValueError('Stage the relocated WiiDS SDL2 first')
    bootstrap_source = ROOT / 'reference/sms-eclipse-thor/android/bootstrap.cpp'
    bootstrap = bootstrap_source.read_text(encoding='utf-8')
    bootstrap = bootstrap.replace('Java_dev_chasem_smslauncher_', 'Java_dev_chasem_eclipselauncher_')
    bootstrap = bootstrap.replace('/libsms.so', '/libeclipse.so').replace('loading libsms.so', 'loading libeclipse.so')
    cpp = OUT / 'bootstrap.cpp'
    cpp.write_text(bootstrap, encoding='utf-8', newline='\n')
    include = ROOT / 'build/eclipse-thor/android-native/SDL2'
    target = OUT / 'libeclipsebootstrap.so'
    run(NDK / 'bin/clang++.exe', '--target=aarch64-none-linux-android30',
        f'--sysroot={NDK / "sysroot"}', '-shared', '-fPIC', '-O2', '-std=c++17',
        '-Wl,-soname,libeclipsebootstrap.so', '-Wl,-z,max-page-size=16384',
        '-I' + str(include / 'include/SDL2'),
        '-I' + str(include / 'include-config-relwithdebinfo/SDL2'),
        cpp, '-L' + str(JNI), '-lSDL2', '-llog', '-ldl', '-o', target)
    run(NDK / 'bin/llvm-strip.exe', '--strip-unneeded', target)
    game = ROOT / 'build/eclipse-thor/jniLibs/arm64-v8a/libsms.so'
    shutil.copyfile(target, JNI / target.name)
    shutil.copyfile(game, JNI / 'libeclipse.so')
    for symbol in ('nativeSnapshot', 'nativeSky', 'nativePhoto', 'nativeFloor'):
        if 'Java_dev_chasem_eclipselauncher_ThorSunshineScreen_' + symbol not in run(NDK / 'bin/llvm-nm.exe', '-D', '--defined-only', target):
            raise ValueError('Missing Eclipse JNI bridge: ' + symbol)
    if sha(game) != sha(JNI / 'libeclipse.so'):
        raise ValueError('Eclipse game runtime changed while staging')
    receipt = {'kind': 'embedded-eclipse-runtime', 'java_package': 'dev.chasem.eclipselauncher',
               'processes': [':eclipse', ':eclipse_game'],
               'profile': 'external-files/eclipse', 'disc': 'internal-files/games/eclipse/game.iso',
               'sources': sources, 'bootstrap_source_sha256': sha(bootstrap_source),
               'libraries': {name: sha(JNI / name) for name in ('libeclipse.so', 'libeclipsebootstrap.so', 'libSDL2.so')},
               'roms_saves_hud_staged': False}
    if args.bundle_patch:
        patch = ROOT / 'build/eclipse-thor/mod/patches/v1.1.0.xdelta'
        if patch.stat().st_size != 1191516980 or sha(patch) != '25c6b08333802bca17c1e6571c7289a6382a32a85af1aa5bd224acfbac6a8bf0':
            raise ValueError('The bundled Eclipse patch differs from the official pinned 1.1.0 release')
        bundled = ROOT / 'build/wiids-merge/assets/mods/eclipse-v1.1.0.xdelta'
        bundled.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(patch, bundled)
        if sha(bundled) != sha(patch):
            raise ValueError('Eclipse patch changed while staging')
        receipt['patch_asset'] = {'path': str(bundled.relative_to(ROOT)), 'bytes': bundled.stat().st_size, 'sha256': sha(bundled)}
    (OUT / 'staging.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
