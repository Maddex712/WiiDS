"""Build only the Galaxy picture-mode change from the accepted integrated native tree."""
from pathlib import Path
import hashlib,json,shutil,subprocess,zipfile
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/dolphin-import-2026-10-25/native';O.mkdir(parents=True,exist_ok=True)
N=R/'build/galaxy-integration/native';S=R/'build/galaxy-integration/source';W=R/'reference/galaxy-android'
tool=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
old=O/'baseline-game.so'
subprocess.run([str(tool/'llvm-strip.exe'),'--strip-unneeded','-o',str(old),str(N/'libgame.so')],check=True)
with zipfile.ZipFile(R/'build/feature-1.1/WiiDS-1.1-dev.15.apk') as z:expected=hashlib.sha256(z.read('lib/arm64-v8a/libgame.so')).hexdigest()
assert sha(old)==expected,'Native build is not the accepted runtime; stop before edits'
files=['platform/src/gx/vk/vk_presentation_pipeline.cpp','platform/src/gx/vk/vk_texture_presenter.cpp','platform/mobile/thor/ThorSettingsBridge.cpp','platform/mobile/ui/Settings.cpp','platform/mobile/ui/Settings.h']
records={}
for name in files:
 backup=O/'before'/name;assert not backup.exists();backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(S/name,backup)
 records[name]={'before':sha(S/name),'after':sha(W/name)};shutil.copy2(W/name,S/name)
(O/'source-delta.json').write_text(json.dumps(records,indent=2))
with (O/'build.log').open('w') as log:
 result=subprocess.run([str(R/'.tools/host-tools/cmake/data/bin/ninja.exe'),'-C',str(N),'-j4','game'],stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,'Inspect native/build.log'
subprocess.run([str(tool/'llvm-strip.exe'),'--strip-unneeded','-o',str(O/'libgame.so'),str(N/'libgame.so')],check=True)
assert sha(O/'libgame.so')!=expected
report={'status':'PASS','baseline_sha256':expected,'libgame_sha256':sha(O/'libgame.so'),'source_delta':records,'scope':'Actual ARM64 compile/link; physical shader/device verification remains separate'}
(O/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
