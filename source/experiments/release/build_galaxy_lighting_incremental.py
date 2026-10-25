"""Rebuild an explicitly verified local Galaxy native profile; no device work."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess
R=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--prior',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--source',action='append',required=True)
a=p.parse_args()
N=R/'build/galaxy-integration/native';S=R/'build/galaxy-integration/source'
W=R/'reference/galaxy-android';O=a.output.resolve()
assert O.is_relative_to(R/'build') and not (O/'result.json').exists()
O.mkdir(parents=True,exist_ok=True)
tool=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
prior=json.loads(a.prior.read_text(encoding='utf-8'))
assert sha(a.prior.parent/'libgame.so')==prior['libgame_sha256']
subprocess.run([str(tool/'llvm-strip.exe'),'--strip-unneeded','-o',str(O/'input-game.so'),str(N/'libgame.so')],check=True)
assert sha(O/'input-game.so')==prior['libgame_sha256'],'Unexpected native baseline'
records=prior['source_delta']
for name,e in records.items():assert sha(S/name)==e['after'],name
for name in a.source:
    assert (W/name).resolve().is_relative_to(W)
    before=sha(S/name)
    backup=O/'before'/name;backup.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(S/name,backup)
    records.setdefault(name,{'before':before})['after']=sha(W/name)
    shutil.copy2(W/name,S/name)
    # Restoring an older file can otherwise leave a newer diagnostic object
    # in the build: force Ninja to rebuild every explicitly selected source.
    os.utime(S/name,None)
with (O/'build.log').open('w',encoding='utf-8') as log:
    result=subprocess.run([str(R/'.tools/host-tools/cmake/data/bin/ninja.exe'),'-C',str(N),'-j4','game'],stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,'Inspect native build.log'
subprocess.run([str(tool/'llvm-strip.exe'),'--strip-unneeded','-o',str(O/'libgame.so'),str(N/'libgame.so')],check=True)
shutil.copy2(O/'libgame.so',R/'build/feature-1.1/preview-dev9/source/reference/driftdroid/android/app/src/main/jniLibs/arm64-v8a/libgame.so')
report={'status':'PASS','baseline_sha256':prior['baseline_sha256'],
        'input_sha256':prior['libgame_sha256'],'libgame_sha256':sha(O/'libgame.so'),
        'source_delta':records,'new_changed_sources':a.source,
        'scope':'Local native build; physical scene verification is separate'}
(O/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='source_delta'},indent=2))
