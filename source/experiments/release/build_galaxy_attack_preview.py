"""Build the signed-diffuse correction atop the verified dev21 native profile."""
from pathlib import Path
import hashlib,json,shutil,subprocess
R=Path(__file__).resolve().parents[2]
O=R/'build/feature-1.1/galaxy-attack-lighting-2026-10-25/native'
O.mkdir(parents=True,exist_ok=True)
N=R/'build/galaxy-integration/native';S=R/'build/galaxy-integration/source'
W=R/'reference/galaxy-android'
tool=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
prior=json.loads((O.parent/'before/native-result.json').read_text())
assert prior['libgame_sha256']=='7846d4e1ceefa2d4de12fc257a1beb432ff5f3e3b3b27cd08b7a183914a1fd19'
baseline=O/'baseline-game.so'
subprocess.run([str(tool/'llvm-strip.exe'),'--strip-unneeded','-o',str(baseline),str(N/'libgame.so')],check=True)
assert sha(baseline)==prior['libgame_sha256'],'Unexpected native baseline'
name='platform/src/gx/vk_shadergen_vs.cpp'
assert sha(S/name)==prior['source_delta'][name]['after']
assert sha(O.parent/'before/vk_shadergen_vs.cpp')==sha(S/name)
shutil.copy2(W/name,S/name)
records=prior['source_delta'];records[name]['after']=sha(W/name)
with (O/'build.log').open('w',encoding='utf-8') as log:
    result=subprocess.run([str(R/'.tools/host-tools/cmake/data/bin/ninja.exe'),'-C',str(N),'-j4','game'],stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,'Inspect native build.log'
subprocess.run([str(tool/'llvm-strip.exe'),'--strip-unneeded','-o',str(O/'libgame.so'),str(N/'libgame.so')],check=True)
shutil.copy2(O/'libgame.so',R/'build/feature-1.1/preview-dev9/source/reference/driftdroid/android/app/src/main/jniLibs/arm64-v8a/libgame.so')
report={'status':'PASS','baseline_sha256':prior['baseline_sha256'],
        'input_dev21_sha256':prior['libgame_sha256'],'libgame_sha256':sha(O/'libgame.so'),
        'source_delta':records,'new_changed_sources':[name],
        'scope':'Signed diffuse preserves negative light until accumulator clamp; scene check separate'}
(O/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='source_delta'},indent=2))
