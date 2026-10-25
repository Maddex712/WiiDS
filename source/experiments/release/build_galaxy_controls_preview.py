"""Continue the accepted dev17 native build with relative touch, native camera turns and GX material modulation."""
from pathlib import Path
import hashlib,json,shutil,subprocess
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/galaxy-controls-lighting-2026-10-25/native'
O.mkdir(parents=True,exist_ok=True)
N=R/'build/galaxy-integration/native';S=R/'build/galaxy-integration/source';W=R/'reference/galaxy-android'
tool=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
baseline17='0ddb8be1dc76b697cad1ff1c02ceec5931318f5b4517ee5a746dd6b89f398f0a'
old=O/'baseline-game.so'
subprocess.run([str(tool/'llvm-strip.exe'),'--strip-unneeded','-o',str(old),str(N/'libgame.so')],check=True)
assert sha(old)==baseline17,'Stop: native runtime does not match accepted dev17'
prior=json.loads((R/'build/feature-1.1/dolphin-import-2026-10-25/native/result.json').read_text())
files=['platform/mobile/thor/ThorLowerBridge.cpp','platform/mobile/thor/ThorLowerBridge.h','platform/mobile/core/MobileApp.cpp',
 'platform/mobile/input/GamepadInput.cpp','platform/mobile/input/GamepadInput.h','platform/mobile/thor/TrackpadMailbox.h',
 'platform/include/port/camera_stick.h','decomp/src/Game/Camera/CameraDirector.cpp','decomp/src/Game/Camera/CameraLocalUtil.cpp',
 'decomp/include/Game/Camera/CameraLocalUtil.hpp','platform/src/gx/vk_shadergen_vs.cpp']
records=dict(prior['source_delta'])
for name in files:
 backup=O/'before'/name;assert not backup.exists();backup.parent.mkdir(parents=True,exist_ok=True)
 before=sha(S/name) if (S/name).exists() else None
 if before:shutil.copy2(S/name,backup)
 records[name]={'before':before,'after':sha(W/name)}
 (S/name).parent.mkdir(parents=True,exist_ok=True);shutil.copy2(W/name,S/name)
(O/'source-delta.json').write_text(json.dumps(records,indent=2))
with (O/'build.log').open('w') as log:
 result=subprocess.run([str(R/'.tools/host-tools/cmake/data/bin/ninja.exe'),'-C',str(N),'-j4','game'],stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,'Inspect native/build.log'
subprocess.run([str(tool/'llvm-strip.exe'),'--strip-unneeded','-o',str(O/'libgame.so'),str(N/'libgame.so')],check=True)
assert sha(O/'libgame.so')!=baseline17
report={'status':'PASS','baseline_sha256':prior['baseline_sha256'],'input_dev17_sha256':baseline17,
 'libgame_sha256':sha(O/'libgame.so'),'source_delta':records,
 'scope':'Actual ARM64 compile/link; device input and scene appearance remain separate acceptance checks'}
(O/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
