"""Cross-check Java conversion with Galaxy's existing C++ converter on actual ARM64 Android."""
from pathlib import Path
import argparse,hashlib,json,subprocess
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/dolphin-import-2026-10-25/native-codec';O.mkdir(parents=True,exist_ok=True)
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);a=p.parse_args()
F=O.parent/'tests/fixtures';S=R/'reference/galaxy-android/platform/mobile/ui'
(O/'check.cpp').write_text(r'''#include "DolphinSaveConverter.h"
#include <fstream>
#include <iterator>
#include <cstdio>
std::vector<uint8_t> read(const char* name){std::ifstream f(name,std::ios::binary);return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});}
int main(int argc,char** argv){if(argc!=3)return 10;auto native=read(argv[1]),java=read(argv[2]);std::vector<uint8_t> retail,back;
 if(!mobile_ui::DolphinSaveConverter::isNativeSave(native.data(),native.size()))return 11;
 if(!mobile_ui::DolphinSaveConverter::convertNativeToDolphin(native.data(),native.size(),retail))return 12;
 if(retail!=java)return 13;
 if(!mobile_ui::DolphinSaveConverter::convertDolphinToNative(java.data(),java.size(),back)||back!=native)return 14;
 puts("PASS: Java and existing C++ conversion are byte-identical in both directions on ARM64");}
''')
compiler=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang++.cmd'
subprocess.run([str(compiler),'-std=c++17','-O1','-static-libstdc++','-I'+str(S),str(O/'check.cpp'),str(S/'DolphinSaveConverter.cpp'),'-o',str(O/'codec-check')],check=True)
adb=[str(R/'.tools/platform-tools/adb.exe'),'-s',a.serial]
state=subprocess.check_output(adb+['shell','dumpsys','activity','activities'],text=True)
assert not any('topResumedActivity=' in s and 'io.github.maddex712.wiids/' in s and 'ModePickerActivity' not in s for s in state.splitlines()),'Owner game foreground; abort'
paths=[('/data/local/tmp/wiids-dolphin-codec-check',O/'codec-check'),('/data/local/tmp/wiids-dolphin-native-fixture',F/'galaxy-native'),('/data/local/tmp/wiids-dolphin-be-fixture',F/'galaxy-be')]
try:
 for remote,source in paths:subprocess.run(adb+['push',str(source),remote],check=True,capture_output=True)
 subprocess.run(adb+['shell','chmod','700',paths[0][0]],check=True)
 run=subprocess.run(adb+['shell',*[r for r,_ in paths]],capture_output=True,text=True,timeout=30);print(run.stdout,run.stderr);run.check_returncode()
 (O/'result.json').write_text(json.dumps({'status':'PASS','output':run.stdout,'converter_sha256':hashlib.sha256((S/'DolphinSaveConverter.cpp').read_bytes()).hexdigest()},indent=2))
finally:
 for remote,_ in paths:subprocess.run(adb+['shell','rm','-f',remote],check=True,capture_output=True)
