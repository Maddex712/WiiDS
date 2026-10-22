"""Build the Galaxy add-on's on-device importer: the disc extractor and cooker bridge
(reference/galaxy-android/installer-mobile/jni, as libinstaller.so), its embedded Python 3.10,
nodtool, and our own cooker scripts (reference/galaxy-android/tools/cook + tools/rarc.py, the
ones the PC import used), so a disc imported on the Thor cooks exactly as on the PC.

Two changes are made to the scripts as they are bundled, for the embedded interpreter only:
  - cook.convert() runs its jobs on threads (an app cannot spawn Python processes), and
  - rarc.yaz0_decompress() uses the bridge's native Yaz0 decoder when it is there, and
  - collision.FROM_MODEL keeps the crater-collision rebuild that upstream turns off on Android.
Outputs build/galaxy-thor/importer/{libs/*.so, cooker-scripts.zip, pythonhome.zip}; Package-Runtime.py
packages them when present. Never touches game data.
"""
from pathlib import Path
import io
import importlib.util
import json
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / 'reference/galaxy-android'
INSTALLER = UPSTREAM / 'installer-mobile'
OUT = ROOT / 'build/galaxy-thor/importer'
NDK = ROOT / '.tools/android-sdk/ndk/29.0.14206865'
CMAKE = ROOT / '.tools/host-tools/cmake/data/bin/cmake.exe'
NINJA = ROOT / '.tools/host-tools/cmake/data/bin/ninja.exe'
PYTHON_LIBS = ['libpython3.10.so', 'libffi.so', 'liblzma.so', 'libbz2.so']

THREADS = '''
    # The WiiDS Galaxy add-on runs this inside its own process (embedded Python, android_bridge):
    # no Python executable to spawn, so the jobs run on threads there.
    if 'android_bridge' in sys.modules:
        ex_factory = lambda: concurrent.futures.ThreadPoolExecutor(max_workers=min(4, args.jobs or 4))
    else:
        ex_factory = lambda: concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs,
            mp_context=multiprocessing.get_context('spawn'))
    with ex_factory() as ex:'''
YAZ0 = '''def yaz0_decompress(data):
    if data[:4] != b'Yaz0':
        return data
    try:  # the WiiDS Galaxy add-on's native decoder (installer-mobile/jni)
        import android_bridge
        if hasattr(android_bridge, 'yaz0_decompress'):
            return android_bridge.yaz0_decompress(data)
    except ImportError:
        pass'''


def run(*args, cwd=None):
    subprocess.run([str(a) for a in args], check=True, cwd=cwd)


def build_native():
    build = OUT / 'cmake'
    run(CMAKE, '-S', INSTALLER / 'jni', '-B', build, '-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={NINJA}',
        f'-DCMAKE_TOOLCHAIN_FILE={NDK}/build/cmake/android.toolchain.cmake', '-DANDROID_ABI=arm64-v8a',
        '-DANDROID_PLATFORM=android-32', '-DCMAKE_BUILD_TYPE=Release')
    run(CMAKE, '--build', build, '--parallel', '8')
    libs = OUT / 'libs'
    libs.mkdir(parents=True, exist_ok=True)
    shutil.copy2(build / 'libinstaller.so', libs / 'libinstaller.so')
    for name in PYTHON_LIBS:
        shutil.copy2(INSTALLER / 'vendor/python/lib/arm64-v8a' / name, libs / name)
    shutil.copy2(INSTALLER / 'vendor/nodtool/libnodtool.so', libs / 'libnodtool.so')


def bundle_scripts():
    sources = sorted((UPSTREAM / 'tools/cook').glob('*.py')) + [UPSTREAM / 'tools/rarc.py']
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sources:
            text = path.read_text(encoding='utf-8').replace('\r\n', '\n')
            if path.name == 'cook.py':
                old = '''
    # The desktop installer calls this from a thread while Tk is running.
    # Spawn fresh workers rather than forking the GUI process on Linux.
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs,
            mp_context=multiprocessing.get_context('spawn')) as ex:'''
                assert text.count(old) == 1, 'cook.py changed: update Build-Importer.py'
                text = text.replace(old, THREADS)
                text = text.replace('ex.map(process, jobs, chunksize=8)', 'ex.map(process, jobs)')
            if path.name == 'collision.py':
                # Upstream skips the crater-collision rebuild on Android (FROM_MODEL empty there);
                # we keep it, so the phone's cook matches the PC's byte for byte.
                old = "FROM_MODEL = {} if os.environ.get('ANDROID_ROOT') or os.environ.get('ANDROID_DATA') else {"
                assert text.count(old) == 1, 'collision.py changed: update Build-Importer.py'
                text = text.replace(old, 'FROM_MODEL = {')
            if path.name == 'rarc.py':
                old = '''def yaz0_decompress(data):
    if data[:4] != b'Yaz0':
        return data'''
                assert text.count(old) == 1, 'rarc.py changed: update Build-Importer.py'
                text = text.replace(old, YAZ0)
            z.writestr(path.name, text)
    (OUT / 'cooker-scripts.zip').write_bytes(buf.getvalue())
    # The on-device lower screen is generated from the same source as the PC tool.
    # Preserve the scripts' root-relative structure; no game-derived output goes here.
    here = Path(__file__).parent
    with zipfile.ZipFile(OUT / 'cooker-scripts.zip', 'a', zipfile.ZIP_DEFLATED) as z:
        for name in ['LowerAssets.py', 'Extract-LowerArt.py', 'Extract-LowerFonts.py',
                     'Bake-LowerMaps.py', 'Bake-LowerPlanets.py']:
            z.write(here / name, 'experiments/galaxy-thor/' + name)
        for p in sorted((here / 'lowermap').glob('*.py')):
            z.write(p, 'experiments/galaxy-thor/lowermap/' + p.name)
        z.write(UPSTREAM / 'tools/rarc.py', 'reference/galaxy-android/tools/rarc.py')
        spec=importlib.util.spec_from_file_location('lowerplanets',here/'Bake-LowerPlanets.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        z.writestr('experiments/galaxy-thor/factory.json',json.dumps(module.factory(),sort_keys=True))
    shutil.copy2(INSTALLER / 'assets/pythonhome.zip', OUT / 'pythonhome.zip')
    with zipfile.ZipFile(OUT / 'pythonhome.zip', 'a', zipfile.ZIP_DEFLATED) as z:
        site=ROOT/'build/galaxy-thor/python-packages/site-packages'
        for p in sorted(site.rglob('*')):
            if p.is_file() and not p.relative_to(site).as_posix().startswith('chaquopy/lib/'):
                z.write(p, 'lib/python3.10/site-packages/' + p.relative_to(site).as_posix())


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    run(__import__('sys').executable, Path(__file__).parent / 'Prepare-PythonPackages.py')
    build_native()
    bundle_scripts()
    for p in sorted(OUT.rglob('*')):
        if p.is_file() and 'cmake' not in p.parts:
            print(f'{p.relative_to(OUT)}  {p.stat().st_size}')


if __name__ == '__main__':
    main()
