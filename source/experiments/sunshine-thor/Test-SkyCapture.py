"""Compile/run actual sky capture source against deterministic GL/queue mocks on Windows.

No game data or device. The freestanding DLL avoids requiring a host C++ SDK.
"""
from pathlib import Path
import ctypes
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/sunshine-thor/sky-tests'
CLANG = ROOT / '.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'


def main():
    source = (ROOT / 'reference/sms-thor/platform/gx/src/gx_render.cpp').read_text()
    assert '// BEGIN THOR ASYNC SKY' in source, 'Old synchronous title capture has no async sky path'
    code = source.split('// BEGIN THOR ASYNC SKY', 1)[1].split('// END THOR ASYNC SKY', 1)[0]
    snapshot = (ROOT / 'reference/sms-thor/android/thor_snapshot.cpp').read_text()
    snapshot = snapshot.split('// The title\'s sky', 1)[1].split('// --- Thor HUD split', 1)[0]
    snapshot = '// The title\'s sky' + snapshot
    OUT.mkdir(parents=True, exist_ok=True)
    harness = (Path(__file__).parent / 'sky_capture_mock.cpp').read_text().replace('/* ACTUAL_SOURCE */', code)
    harness = harness.replace('/* SNAPSHOT_SOURCE */', snapshot)
    cache_header = (ROOT / 'reference/sms-thor/platform/gx/src/gx_glcache.h').read_text()
    cache_struct = cache_header.split('struct GlCache {', 1)[1].split('};', 1)[0]
    cache_helpers = cache_header.split('inline void glcActiveUnit', 1)[1].split('inline void glcBindVertexArray', 1)[0]
    cache_invalidate = source.split('void glcInvalidate() {', 1)[1].split('\n}', 1)[0]
    harness = harness.replace('/* CACHE_STRUCT */', 'struct GlCache {' + cache_struct + '};\nstatic GlCache g_glc;')
    harness = harness.replace('/* CACHE_SOURCE */', 'static void glcInvalidate() {' + cache_invalidate + '\n}\n' +
                              'inline void glcActiveUnit' + cache_helpers)
    cpp = OUT / 'capture.cpp'
    dll = OUT / 'capture.dll'
    obj = OUT / 'capture.obj'
    cpp.write_text(harness)
    result = subprocess.run([str(CLANG), '--target=x86_64-pc-windows-msvc', '-std=c++17',
        '-nostdlib', '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-fno-builtin',
        '-c', str(cpp), '-o', str(obj)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    result = subprocess.run([str(CLANG.parent / 'ld.lld.exe'), '-flavor', 'link',
        '/dll', '/noentry', '/nodefaultlib', str(obj), '/out:' + str(dll)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    lib = ctypes.CDLL(str(dll))
    result = lib.run_tests()
    assert result == 0, f'Actual-source sky capture mock failed at assertion {result}'
    print('PASS 31 actual-source async sky GL/queue/pixel/cadence/snapshot/cache-state checks')


if __name__ == '__main__':
    main()
