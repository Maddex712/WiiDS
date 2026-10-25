"""Verify the actual VirtualRemote shake waveform against NSMB's motion contract on ARM64."""
from pathlib import Path
import argparse,json,re,subprocess

R=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--serial',required=True);args=parser.parse_args()
O=R/'build/feature-1.1/shake/native-test';O.mkdir(parents=True,exist_ok=True)
source=(R/'reference/nsmbw-android-e1/runtime/src/virtual_remote.cpp').read_text()
def block(marker):
 start=source.index(marker);begin=source.index('{',start);depth=1
 for end in range(begin+1,len(source)):
  depth+=(source[end]=='{')-(source[end]=='}')
  if depth==0:return source[start:end+1]
 raise AssertionError('Unclosed production block')
constants='\n'.join(re.search(r'^constexpr .*\b'+name+r'\s*=.*$',source,re.M).group(0) for name in ('kShakeSwingG','kShakeFrames','kMaxTiltRadians','kTiltFollow'))
fixture='''#include "wii_remote_input.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
'''+constants+'\n'+block('struct Intent {')+';\n'+block('struct Motion {')+';\n'+block('void ApplyMotion(')+'''
int main(){
 int checks=0;auto check=[&](bool value){assert(value);++checks;};
 for(float tilt:{-1.f,0.f,1.f}) {
  Motion motion;Intent intent;intent.tilt=tilt;
  WiiRemoteInput::KpadSample sample{};
  for(int i=0;i<40;i++)ApplyMotion(motion,intent,sample);
  float previous=sample.acc[1];int streak=0;intent.shake=true;
  // One sensor pulse produces >=4 consecutive shake-sized changes, as setShakeY requires.
  for(int frame=0;frame<10;frame++) {
   ApplyMotion(motion,intent,sample);intent.shake=false;
   if(std::fabs(sample.acc[1]-previous)>=.28f)++streak;else streak=0;
   previous=sample.acc[1];check(std::isfinite(sample.acc[1]));
  }
  check(streak>=4);check(sample.hold==0);
  for(int i=0;i<20;i++)ApplyMotion(motion,intent,sample);
  check(motion.shakeFramesLeft==0);
  check(std::fabs(sample.acc[1]*sample.acc[1]+sample.acc[2]*sample.acc[2]-1.f)<.001f);
  check(tilt==0 ? sample.acc[2]==0 : (sample.acc[2]*tilt>0));
  // Sustained shaking is still motion input every frame, then settles after release.
  intent.shake=true;
  for(int frame=0;frame<60;frame++){ApplyMotion(motion,intent,sample);check(motion.shakeFramesLeft>0);}
  intent.shake=false;for(int i=0;i<20;i++)ApplyMotion(motion,intent,sample);
  check(motion.shakeFramesLeft==0);
  intent.shake=true;ApplyMotion(motion,intent,sample);check(motion.shakeFramesLeft>0);
 }
 std::printf("PASS %d actual-source shake waveform checks (tilt -1/0/+1)\\n",checks);
}
'''
(O/'test.cpp').write_text(fixture)
cc=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang++.cmd'
subprocess.run([str(cc),'-std=c++17','-O1','-static-libstdc++','-I'+str(R/'reference/nsmbw-android-e1/runtime/include'),str(O/'test.cpp'),'-o',str(O/'test')],check=True)
adb=[str(R/'.tools/platform-tools/adb.exe'),'-s',args.serial];remote='/data/local/tmp/wiids-nsmb-shake-test'
subprocess.run(adb+['push',str(O/'test'),remote],check=True,capture_output=True)
try:
 subprocess.run(adb+['shell','chmod','700',remote],check=True)
 result=subprocess.run(adb+['shell',remote],capture_output=True,text=True,timeout=30)
 assert result.returncode==0,result.stdout+result.stderr
 print(result.stdout.strip());(O/'verification.json').write_text(json.dumps({'status':'PASS','result':result.stdout.strip(),'scope':'Actual production ApplyMotion function, structures/constants and KpadSample header; synthetic requests on physical ARM64, not a human shake gameplay test'},indent=2)+'\n')
finally:subprocess.run(adb+['shell','rm',remote],check=True)
