"""Stage the non-debuggable Galaxy runtime into WiiDS. Never copies game data."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--native-dir',type=Path,default=ROOT/'build/galaxy-thor/native')
args=parser.parse_args()
app=ROOT/'reference/driftdroid/android/app/src/main'
jni=app/'jniLibs/arm64-v8a'
strip=ROOT/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
for p in sorted((HERE/'android').glob('*.java')):
    # Status comes from the package-local profile; no exported helper provider.
    if p.name=='StatusProvider.java':continue
    target=app/'java'/('com/galaxy/installer' if p.name=='CookerEngine.java' else 'dev/thor/galaxy')/p.name
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
libs={'libgame.so':args.native_dir/'libgame.so','libmain.so':args.native_dir/'libmain.so'}
for p in (ROOT/'build/galaxy-thor/importer/libs').glob('*.so'):
    if p.name=='libc++_shared.so':raise RuntimeError('Importer must reuse WiiDS libc++')
    libs[p.name]=p
records={}
for name,path in libs.items():
    if not path.is_file():raise RuntimeError('Missing Galaxy build input: '+str(path))
    subprocess.run([str(strip),'--strip-unneeded','-o',str(jni/name),str(path)],check=True)
    records[name]={'input_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                   'staged_sha256':hashlib.sha256((jni/name).read_bytes()).hexdigest()}
assets=ROOT/'build/wiids-merge/assets/galaxy';assets.mkdir(exist_ok=True)
for name in ['pythonhome.zip','cooker-scripts.zip']:
    shutil.copy2(ROOT/'build/galaxy-thor/importer'/name,assets/name)
shutil.copy2(ROOT/'reference/galaxy-android/assets/galaxy_shader_manifest.bin',assets.parent/'galaxy_shader_manifest.bin')
licenses=app/'assets/licenses';licenses.mkdir(parents=True,exist_ok=True)
for name,path in {
    'LICENSE-Galaxy.txt':ROOT/'reference/galaxy-android/LICENSE',
    'LICENSE-Petari.txt':ROOT/'reference/galaxy-android/decomp/LICENSE-Petari.txt',
    'THIRD-PARTY-NOTICES-Galaxy.md':ROOT/'reference/galaxy-android/THIRD_PARTY_NOTICES.md',
    'LICENSE-FreeType-FTL.txt':ROOT/'release/licenses/LICENSE-FreeType-FTL.txt',
    'GCC-COPYING3.txt':ROOT/'release/licenses/GCC-COPYING3.txt',
    'GCC-RUNTIME-EXCEPTION.txt':ROOT/'release/licenses/GCC-RUNTIME-EXCEPTION.txt',
    'LICENSE-CPython.txt':ROOT/'release/licenses/LICENSE-CPython.txt',
}.items():shutil.copy2(path,licenses/name)
site=ROOT/'build/galaxy-thor/python-packages/site-packages'
for p in site.rglob('*'):
    if p.is_file() and p.name.upper().startswith(('LICENSE','COPYING','NOTICE')):
        target=licenses/'galaxy-python'/p.relative_to(site);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
out=ROOT/'build/galaxy-integration';out.mkdir(exist_ok=True)
(out/'integrated-staging.json').write_text(json.dumps({'libraries':records,'game_data_bundled':False,
    'helper_required':False,'wrap_sh':False,'debuggable':False},indent=2)+'\n')
code_assets={}
for name in ['galaxy/pythonhome.zip','galaxy/cooker-scripts.zip','galaxy_shader_manifest.bin']:
    p=assets.parent/name
    code_assets['assets/'+name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size,
        'kind':'Python code/dependencies and source-generated metadata' if name.endswith('.zip') else 'upstream generated shader-code manifest'}
(ROOT/'release/galaxy-code-assets.json').write_text(json.dumps(code_assets,indent=2)+'\n')
print('Integrated Galaxy staged:',len(records),'native libraries; no helper or wrap.sh')
