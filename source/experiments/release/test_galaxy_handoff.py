"""Actual Galaxy input code: physical bindings plus touch/stick cursor handoff on ARM64."""
from pathlib import Path
import json,shutil,subprocess
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/handoff-tests';S=R/'reference/galaxy-android'
for folder in ('input','ui'):(O/folder).mkdir(parents=True,exist_ok=True)
for name in ('GamepadInput.cpp','GamepadInput.h'):shutil.copy2(S/'platform/mobile/input'/name,O/'input'/name)
(O/'ui/UIOverlay.h').write_text('''#pragma once
#include <string>
namespace mobile_ui {
struct Settings {float stickSensitivity=1,starStickSensitivity=1;int rightStickMode=1;};
class UIOverlay { Settings s;float x=0,y=0;public:
static UIOverlay& get(){static UIOverlay o;return o;} Settings& settings(){return s;}
void showToast(const std::string&){} bool isSettingsOpen(){return false;}void closeSettings(){}
void setPointerPos(float a,float b){x=a;y=b;}float getPointerX(){return x;}float getPointerY(){return y;}void releasePointer(){}
};}
''')
fixture=(R/'experiments/galaxy-thor/tests/gamepad.cpp').read_text()
fixture='#include "ui/UIOverlay.h"\nextern "C" int port_input_menu_pointer(void){return 0;}\n'+fixture
fixture=fixture.replace('int main() {','extern "C" int32_t AKeyEvent_getRepeatCount(const AInputEvent*) { return 0; }\nint main() {')
marker='    printf("PASS: %d actual GamepadInput source assertions\\n", checks);'
assert marker in fixture
fixture=fixture.replace(marker,'''    auto& overlay=mobile_ui::UIOverlay::get();
    motion.source=AINPUT_SOURCE_JOYSTICK;
    motion.axes[AMOTION_EVENT_AXIS_Z]=0;
    input.handleMotionEvent(&motion,pad,spin,false);
    overlay.setPointerPos(.7f,-.3f);
    pad.pointerX=.7f;pad.pointerY=-.3f;pad.pointerValid=1;
    input.updatePointer(pad,1.f/60,true);
    check(std::fabs(pad.pointerX-.7f)<.0001f && std::fabs(pad.pointerY+.3f)<.0001f);
    input.updatePointer(pad,1.f/60,false);
    check(std::fabs(pad.pointerX-.7f)<.0001f && std::fabs(pad.pointerY+.3f)<.0001f);
    motion.axes[AMOTION_EVENT_AXIS_Z]=1;
    input.handleMotionEvent(&motion,pad,spin,false);
    input.updatePointer(pad,1.f/60,true);check(std::fabs(pad.pointerX-.7f)<.0001f);
    input.updatePointer(pad,1.f/60,false);check(pad.pointerX>.7f && pad.pointerX<.8f);
'''+marker)
(O/'test.cpp').write_text(fixture)
cc=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang++.cmd'
binary=O/'test';remote='/data/local/tmp/wiids-galaxy-handoff-test';adb=[str(R/'.tools/platform-tools/adb.exe')]
subprocess.run([str(cc),'-std=c++17','-O1','-static-libstdc++','-I'+str(O/'input'),'-I'+str(S/'platform/include'),str(O/'input/GamepadInput.cpp'),str(O/'test.cpp'),'-llog','-o',str(binary)],check=True)
subprocess.run(adb+['push',str(binary),remote],check=True,capture_output=True)
try:
 subprocess.run(adb+['shell','chmod','700',remote],check=True)
 result=subprocess.run(adb+['shell',remote],capture_output=True,text=True,timeout=30)
 assert result.returncode==0,result.stdout+result.stderr
 print(result.stdout.strip());(O/'verification.json').write_text(json.dumps({'status':'PASS','result':result.stdout.strip(),'scope':'Actual GamepadInput with mocked input events on physical ARM64; not live gameplay'},indent=2)+'\n')
finally:subprocess.run(adb+['shell','rm',remote],check=True)
