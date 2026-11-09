"""Pin native sources, verify an owner's complete DOL, and build the ARM64 lab.

All downloaded material is public source. Never downloads a disc or game asset.
--dol supports compile-only progress while a disc transfer is unfinished.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

DECOMP_PIN = 'b56cc7677fa23058eff8f38784479c3b4b854060'
RUNTIME_PIN = 'bd36eb982ea58c6651584cc48264876d1aba9546'
GCN_PIN = 'b2193d7c7ce65c40959cf81859beaacafeebfd99'
DOL_SHA1 = 'e389e50357d4a5076ab3d38672204658f411123d'
ROOT = Path(__file__).resolve().parents[2]


def run(args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def pin(url, target, commit):
    if not target.exists():
        run(['git', 'clone', '--no-checkout', '--filter=blob:none', url, target])
        run(['git', '-C', target, 'fetch', '--depth=1', 'origin', commit])
        run(['git', '-C', target, 'checkout', '--detach', commit])
    head = subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], text=True).strip()
    if head != commit:
        raise ValueError(f'Existing source is at {head}, expected {commit}; nothing reset: {target}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=Path('M:/WiiDS-mkdd-2026-11-01'))
    p.add_argument('--dol', required=True, type=Path)
    p.add_argument('--build', action='store_true')
    p.add_argument('--jobs', type=int, default=6)
    a = p.parse_args()
    if not 1 <= a.jobs <= 32:
        p.error('--jobs must be 1..32')
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    temp = out/'temp'
    temp.mkdir(exist_ok=True)
    os.environ['TEMP'] = os.environ['TMP'] = str(temp)
    # Exact DOL acceptance is independent of the ISO transfer's completion.
    dol = a.dol.resolve(strict=True)
    actual = hashlib.sha1(dol.read_bytes()).hexdigest()
    if actual != DOL_SHA1:
        raise ValueError(f'Unsupported executable: {actual}; expected {DOL_SHA1}')
    source = out/'runtime'
    pin('https://github.com/doldecomp/mkdd.git', out/'mkdd', DECOMP_PIN)
    pin('https://github.com/ethannicholas/mariokartdd-recomp.git', source, RUNTIME_PIN)
    if not (source/'gcn-recomp/CMakeLists.txt').exists():
        run(['git', '-C', source, 'submodule', 'update', '--init', '--depth=1'])
    pin('https://github.com/ethannicholas/gcn-recomp.git', source/'gcn-recomp', GCN_PIN)
    generated = out/'generated'
    import sys
    run([sys.executable, '-B', source/'gcn-recomp/recomp/recomp.py', dol,
         source/'analysis/symbols.txt', generated, source/'recomp'])
    cmake = ROOT/'.tools/host-tools/cmake/data/bin/cmake.exe'
    ninja = cmake.with_name('ninja.exe')
    ndk = ROOT/'.tools/android-sdk/ndk/27.2.12479018'
    build = out/'android-arm64'
    run([cmake, '-S', Path(__file__).parent/'native', '-B', build, '-G', 'Ninja',
         f'-DCMAKE_MAKE_PROGRAM={ninja.as_posix()}',
         f'-DCMAKE_TOOLCHAIN_FILE={(ndk/"build/cmake/android.toolchain.cmake").as_posix()}',
         '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-30', '-DANDROID_STL=c++_static',
         '-DCMAKE_BUILD_TYPE=Release', f'-DMKDD_RUNTIME={source.as_posix()}',
         f'-DMKDD_GENERATED={generated.as_posix()}'])
    if a.build:
        with (out/'native-build.log').open('w', encoding='utf-8') as log:
            run([cmake, '--build', build, '--parallel', a.jobs], stdout=log, stderr=subprocess.STDOUT)
    receipt = {'status': 'native_compile_pass' if a.build else 'configured',
               'decomp_pin': DECOMP_PIN, 'runtime_pin': RUNTIME_PIN, 'gcn_pin': GCN_PIN,
               'dol_sha1': actual, 'complete_disc_verified': False, 'game_boot_verified': False,
               'apk_built': False, 'device_accessed': False}
    receipt['native_source_sha256'] = {
        str(f.relative_to(Path(__file__).parent)).replace('\\','/'): hashlib.sha256(f.read_bytes()).hexdigest()
        for f in sorted((Path(__file__).parent/'native').glob('*')) if f.is_file()}
    for name in ('libwiids_mkdd.so', 'mkdd_egl', 'lower_state_test'):
        binary = build/name
        if a.build and binary.exists():
            receipt[name] = {'bytes': binary.stat().st_size,
                             'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
    (out/'compile-verification.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
