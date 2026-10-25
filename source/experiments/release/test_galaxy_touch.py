from pathlib import Path
import hashlib,json,subprocess
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1';O.mkdir(exist_ok=True)
compiler=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang++.cmd'
header=R/'reference/galaxy-android/platform/mobile/thor/TouchPointerMailbox.h'
binary=O/'test-galaxy-touch';remote='/data/local/tmp/wiids-feature-touch-test'
subprocess.run([str(compiler),'-std=c++17','-O1','-static-libstdc++','-pthread','-I'+str(header.parent),str(R/'experiments/galaxy-thor/tests/touch_pointer.cpp'),'-o',str(binary)],check=True)
adb=[str(R/'.tools/platform-tools/adb.exe')]
subprocess.run(adb+['push',str(binary),remote],check=True,capture_output=True)
try:
 subprocess.run(adb+['shell','chmod','700',remote],check=True)
 result=subprocess.run(adb+['shell',remote],capture_output=True,text=True,timeout=30)
 assert result.returncode==0,result.stdout+result.stderr
 print(result.stdout.strip())
 (O/'galaxy-touch-test.json').write_text(json.dumps({'status':'PASS','output':result.stdout.strip(),'mailbox_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),'scope':'Actual mailbox on physical ARM64 Android; not star-bit gameplay'},indent=2)+'\n')
finally:subprocess.run(adb+['shell','rm',remote],check=True)
