"""Mario Kart Wii + Retro Rewind engines with custom GPU driver support.

Same configuration as build_native.py, plus MKW_ANDROID_CUSTOM_GPU_DRIVERS=ON with the pinned
libadrenotools (8fae8ce, the commit upstream DriftDroid uses) from a local checkout, and the
dlopen wrap in runtime/src/android_vulkan_override.cpp in place of the Dawn source patch. Builds
WiiCompiled, RetroRewind and the four adrenotools hook libraries, stripped for jniLibs."""
from pathlib import Path
import hashlib,json,subprocess,sys,time
R=Path(__file__).resolve().parents[2];W=Path('M:/WiiDS-rr-6.13.4');WS=W/'driftdroid';B=W/'android-native-drivers'
OLD=R/'build/android-native/_deps';ADRENO=Path('M:/WiiDS-drivers/libadrenotools');OUT=W/'drivers-libs'
CMAKE=R/'.tools/host-tools/cmake/data/bin/cmake.exe';NINJA=R/'.tools/host-tools/bin/ninja.exe'
NDK=R/'.tools/android-sdk/ndk/27.2.12479018';BIN=NDK/'toolchains/llvm/prebuilt/windows-x86_64/bin'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert subprocess.check_output(['git','-C',str(ADRENO),'rev-parse','HEAD'],text=True).strip()=='8fae8ce254dfc1344527e05301e43f37dea2df80'
# Dawn's DynamicLib dlopen/dlclose renamed in a copy of the stock package, so only Dawn's own
# library loading reaches runtime/src/android_vulkan_override.cpp (see that file).
DAWN=W/'dawn_prebuilt-wiids'
if not (DAWN/'renamed.json').is_file():
    import shutil
    if DAWN.exists():shutil.rmtree(DAWN)
    shutil.copytree(OLD/'dawn_prebuilt-src',DAWN)
    archive=DAWN/'lib/libwebgpu_dawn.a'
    subprocess.run([str(BIN/'llvm-objcopy.exe'),'--redefine-sym','dlopen=wiids_dawn_dlopen',
                    '--redefine-sym','dlclose=wiids_dawn_dlclose',str(archive)],check=True)
    refs=subprocess.check_output([str(BIN/'llvm-nm.exe'),'-A',str(archive)],text=True)
    assert ' U dlopen\n' not in refs and ' U wiids_dawn_dlopen' in refs
    (DAWN/'renamed.json').write_text(json.dumps({'source':str(OLD/'dawn_prebuilt-src'),'sha256':sha(archive)})+'\n')
deps={'ABSEIL-CPP':'abseil-cpp','DAWN_PREBUILT':'dawn_prebuilt','FMT':'fmt','FREETYPE':'freetype','IMGUI':'imgui',
      'PNG':'png','SDL':'sdl','SQLITE3':'sqlite3','TRACY':'tracy','XXHASH':'xxhash','ZSTD':'zstd'}
configure=[str(CMAKE),'-S',str(WS/'runtime'),'-B',str(B),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+NINJA.as_posix(),
    '-DCMAKE_TOOLCHAIN_FILE='+(NDK/'build/cmake/android.toolchain.cmake').as_posix(),'-DANDROID_ABI=arm64-v8a',
    '-DANDROID_PLATFORM=android-29','-DCMAKE_BUILD_TYPE=Release','-DANDROID_STL=c++_shared',
    '-DMKW_ANDROID_CUSTOM_GPU_DRIVERS=ON','-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384',
    '-DWIIDS_RUNTIME_GAME_DATA=ON','-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
    '-DFETCHCONTENT_SOURCE_DIR_ADRENOTOOLS='+ADRENO.as_posix()]
configure+=[f'-DFETCHCONTENT_SOURCE_DIR_{k}={(OLD/(v+"-src")).as_posix()}' for k,v in deps.items() if k!='DAWN_PREBUILT']
configure+=['-DFETCHCONTENT_SOURCE_DIR_DAWN_PREBUILT='+DAWN.as_posix(),'-DDawn_DIR='+(DAWN/'lib/cmake/Dawn').as_posix()]
targets=['WiiCompiled','RetroRewind','main_hook','file_redirect_hook','gsl_alloc_hook','hook_impl']
steps=[('configure',configure),('build',[str(CMAKE),'--build',str(B),*sum((['--target',t] for t in targets),[]),'--parallel','8'])]
for name,cmd in steps:
    if sys.argv[1:] and name not in sys.argv[1:]:continue
    t=time.time()
    with (W/f'drivers-{name}.log').open('w',encoding='utf-8') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
    print(f'{name}: exit {r.returncode} in {time.time()-t:.0f}s',flush=True)
    if r.returncode:sys.exit(f'see drivers-{name}.log')
OUT.mkdir(exist_ok=True);result={}
for lib in ['libWiiCompiled.so','libRetroRewind.so','libmain_hook.so','libfile_redirect_hook.so','libgsl_alloc_hook.so','libhook_impl.so']:
    src=next(B.rglob(lib));out=OUT/lib
    subprocess.run([str(BIN/'llvm-strip.exe'),'--strip-unneeded','-o',str(out),str(src)],check=True)
    result[lib]={'sha256':sha(out),'bytes':out.stat().st_size,'built':str(src)}
(W/'drivers-native-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
