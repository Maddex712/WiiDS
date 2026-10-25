"""Compile tests against actual sources; never connect to or modify the device."""
from pathlib import Path
import os,re,subprocess,json,shutil
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/galaxy-controls-lighting-2026-10-25/tests'
W=R/'reference/galaxy-android';tool=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'
env=dict(os.environ,TMP=str(O),TEMP=str(O))
def compile(name,sources,include):
 subprocess.run([str(tool/'aarch64-linux-android32-clang++.cmd'),'-std=c++17','-O1','-static-libstdc++','-pthread',
 *['-I'+str(p) for p in include],*[str(p) for p in sources],'-o',str(O/name)],check=True,env=env)
compile('trackpad-camera',[R/'experiments/galaxy-thor/tests/trackpad_camera.cpp'],[W/'platform/mobile/thor',W/'platform/include'])
compile('lighting-generate',[R/'experiments/galaxy-thor/tests/lighting_generate.cpp',W/'platform/src/gx/vk_shadergen_vs.cpp',W/'platform/src/gx/vk_shadergen_tables.cpp'],[W/'platform/src/gx',W/'platform/include'])
G=O/'gamepad'
for folder in ('input','ui'):(G/folder).mkdir(parents=True,exist_ok=True)
for name in ('GamepadInput.cpp','GamepadInput.h'):shutil.copy2(W/'platform/mobile/input'/name,G/'input'/name)
shutil.copy2(R/'build/feature-1.1/handoff-tests/ui/UIOverlay.h',G/'ui/UIOverlay.h')
fixture=(R/'experiments/galaxy-thor/tests/gamepad.cpp').read_text()
fixture='#include "ui/UIOverlay.h"\nextern "C" int port_input_menu_pointer(void){return 0;}\n'+fixture
fixture=fixture.replace('int main() {','extern "C" int32_t AKeyEvent_getRepeatCount(const AInputEvent*) { return 0; }\nint main() {')
marker='    printf("PASS: %d actual GamepadInput source assertions\\n", checks);'
assert marker in fixture
fixture=fixture.replace(marker,'''    motion.source=AINPUT_SOURCE_JOYSTICK;
    auto& overlay=mobile_ui::UIOverlay::get();
    overlay.settings().rightStickMode=0;
    motion.axes[AMOTION_EVENT_AXIS_Z]=1;motion.axes[AMOTION_EVENT_AXIS_RZ]=0;
    input.handleMotionEvent(&motion,pad,spin,false);
    for(int i=0;i<120;i++){pad.rightStickX*=.82f;input.updatePointer(pad,1.f/60,false);check(pad.rightStickX==-1);}
    input.reset();input.updatePointer(pad,1.f/60,false);check(pad.rightStickX==0&&pad.rightStickY==0);
    overlay.settings().rightStickMode=1;
    motion.axes[AMOTION_EVENT_AXIS_Z]=0;input.handleMotionEvent(&motion,pad,spin,false);
    overlay.setPointerPos(.7f,-.3f);pad.pointerX=.7f;pad.pointerY=-.3f;pad.pointerValid=1;
    input.updatePointer(pad,1.f/60,true);check(pad.pointerX==.7f&&pad.pointerY==-.3f);
    input.updatePointer(pad,1.f/60,false);check(pad.pointerX==.7f&&pad.pointerY==-.3f);
    motion.axes[AMOTION_EVENT_AXIS_Z]=1;input.handleMotionEvent(&motion,pad,spin,false);
    input.updatePointer(pad,1.f/60,true);check(pad.pointerX==.7f);
    input.updatePointer(pad,1.f/60,false);check(pad.pointerX>.7f&&pad.pointerX<.8f);
'''+marker)
(G/'test.cpp').write_text(fixture)
subprocess.run([str(tool/'aarch64-linux-android32-clang++.cmd'),'-std=c++17','-O1','-static-libstdc++',
 '-I'+str(G/'input'),'-I'+str(W/'platform/include'),str(G/'input/GamepadInput.cpp'),str(G/'test.cpp'),'-llog','-o',str(O/'gamepad-handoff')],check=True,env=env)
stub='''#include <cstdio>
#include <cstdlib>
namespace MR { bool demo=false; bool isDemoActive(){return demo;} }
struct CameraMan { bool left=false,right=false,reset=false;
 bool isEnableToRoundLeft() const{return left;} bool isEnableToRoundRight() const{return right;}
 bool isEnableToReset() const{return reset;} };
struct CameraDirector {
 CameraMan* current=nullptr;CameraMan* mCameraManGame=nullptr;bool mIsStartCameraActive=false,mIsSubjectiveCamera=false;
 CameraMan* getCurrentCameraMan()const{return current;}
 bool isEnableToRoundLeft()const;bool isEnableToRoundRight()const;bool isEnableToReset()const;
};
'''
main='''int main(){int n=0;auto ok=[&](bool v){++n;if(!v){printf("FAIL %d\\n",n);exit(1);}};
 CameraDirector d;ok(!d.isEnableToRoundLeft()&&!d.isEnableToRoundRight()&&!d.isEnableToReset());
 CameraMan game,event;d.current=&game;d.mCameraManGame=&game;
 for(int bits=0;bits<8;bits++){game.left=bits&1;game.right=bits&2;game.reset=bits&4;
 ok(d.isEnableToRoundLeft()==game.left);ok(d.isEnableToRoundRight()==game.right);ok(d.isEnableToReset()==game.reset);}
 game.left=game.right=game.reset=true;
 d.current=&event;ok(!d.isEnableToRoundLeft()&&!d.isEnableToRoundRight()&&!d.isEnableToReset());d.current=&game;
 d.mIsStartCameraActive=true;ok(!d.isEnableToRoundLeft()&&!d.isEnableToRoundRight()&&!d.isEnableToReset());d.mIsStartCameraActive=false;
 d.mIsSubjectiveCamera=true;ok(!d.isEnableToRoundLeft()&&!d.isEnableToRoundRight()&&!d.isEnableToReset());d.mIsSubjectiveCamera=false;
 MR::demo=true;ok(!d.isEnableToRoundLeft()&&!d.isEnableToRoundRight()&&!d.isEnableToReset());
 printf("%d actual camera permission checks PASS\\n",n);}
'''
for label,path in [('old',O.parent/'before/reference/galaxy-android/decomp/src/Game/Camera/CameraDirector.cpp'),('fixed',W/'decomp/src/Game/Camera/CameraDirector.cpp')]:
 text=path.read_text();methods='\n'.join(re.search(r'bool CameraDirector::'+name+r'\(\) const \{.*?\n\}',text,re.S)[0] for name in ['isEnableToRoundLeft','isEnableToRoundRight','isEnableToReset'])
 cpp=O/('camera-permissions-'+label+'.cpp');cpp.write_text(stub+methods+'\n'+main)
 compile('camera-permissions-'+label,[cpp],[])
(O/'native-compiled.json').write_text(json.dumps({'status':'COMPILED','binaries':5,'executed':False,'scope':'ARM64 compilation; no device interaction'},indent=2))
print('Five ARM64 test programs compiled; execution remains pending')
