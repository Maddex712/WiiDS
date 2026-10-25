"""Execute the actual overlay entry with real pinned ImGui frames on Android ARM64."""
from pathlib import Path
import argparse, subprocess, json, hashlib

R = Path(__file__).resolve().parents[2]
P = argparse.ArgumentParser(description=__doc__)
P.add_argument('--serial', required=True)
args = P.parse_args()
O = R/'build/feature-1.1/thor-polish-2026-10-24/overlay-tests'
O.mkdir(parents=True, exist_ok=True)
I = R/'build/nsmbw-thor/e1/android-native/_deps/imgui-src'
B = R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'
A = [str(R/'.tools/platform-tools/adb.exe'), '-s', args.serial]
state = subprocess.check_output(A+['shell','dumpsys','activity','activities'],text=True)
assert not any('topResumedActivity' in l and 'io.github.maddex712.wiids/' in l and
               any(n in l for n in ['GameActivity','MainActivity','GalaxyActivity','$Game']) for l in state.splitlines())

def body(p):
    s=p.read_text(); start=s.index('void Draw() noexcept {'); begin=s.index('{',start); depth=1
    for end in range(begin+1,len(s)):
        depth+=(s[end]=='{')-(s[end]=='}')
        if depth==0:return s[start:end+1]
    raise AssertionError('Incomplete actual overlay')

prefix = r'''
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <cstdlib>
static int draws=0, checks=0;
static bool closeOnWait=false, openOnWait=false;
void aurora_wait_for_frame_worker(){
 if(closeOnWait){ImGui::Render();closeOnWait=false;}
 if(openOnWait){ImGui::NewFrame();openOnWait=false;}
}
namespace WiiRemoteInput{void Poll(){}}
namespace NetplayUi{void Draw(){if(!ImGui::GetBackgroundDrawList())std::abort();++draws;}}
namespace controller_mapping_wizard{void Draw(){}}
namespace settings_overlay{
void ApplyConfiguredMappings(){} void PersistDisplayModeIfChanged(){} void UpdateCursorAutoHide(){}
void UpdateBootShaderState(){} bool StartupScreenVisible(){return false;}
void DrawShaderCompilationStatus(){} void DrawFpsOverlay(){} void DrawTopBar(){}
void DrawExitPrompt(){} void ApplyInputBlockState(){} void DrawStartupScreen(){}
'''
suffix = r'''
}
static void yes(bool ok){++checks;if(!ok){std::printf("FAIL at check %d\n",checks);std::exit(2);}}
int main(){
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(1920,1080);io.DeltaTime=1.f/60.f;
 unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
 ImGui::NewFrame();settings_overlay::Draw();yes(draws==1);
 ImGui::Render();yes(ImGui::GetCurrentContext()->CurrentWindow==nullptr);
 // Exact crash state: the renderer closed its frame, then GXCopyDisp drew overlays.
 settings_overlay::Draw();yes(draws==1);
 for(int n=0;n<1000;++n)settings_overlay::Draw();yes(draws==1);
 for(int n=0;n<25;++n){
  const int previous=draws;ImGui::NewFrame();settings_overlay::Draw();yes(draws==previous+1);
  ImGui::Render();settings_overlay::Draw();yes(draws==previous+1);
 }
 ImGui::NewFrame();auto* saved=GImGui->CurrentWindow;GImGui->CurrentWindow=nullptr;
 const int previous=draws;settings_overlay::Draw();yes(draws==previous);GImGui->CurrentWindow=saved;ImGui::Render();
 // State must be checked after joining the worker, which may end or begin a frame.
 ImGui::NewFrame();closeOnWait=true;settings_overlay::Draw();yes(draws==previous);
 openOnWait=true;settings_overlay::Draw();yes(draws==previous+1);ImGui::Render();
 ImGui::DestroyContext();settings_overlay::Draw();yes(draws==previous+1);
 std::printf("PASS %d real-ImGui production overlay checks\n",checks);
}
'''
results=[]
cases=[('before',O.parent/'overlay-before/e1-settings_overlay.cpp',False),
       ('e1',R/'reference/nsmbw-android-e1/runtime/src/settings_overlay.cpp',True),
       ('msmbwii',R/'reference/nsmbw-android-msmbwii/runtime/src/settings_overlay.cpp',True)]
for name,source,passes in cases:
    cpp=O/(name+'.cpp');cpp.write_text(prefix+body(source)+suffix)
    binary=O/(name+'.bin')
    command=[str(B/'aarch64-linux-android30-clang++.cmd'),'-std=c++20','-O1','-g','-static-libstdc++',
             '-I'+str(I),str(cpp),*[str(I/n) for n in ['imgui.cpp','imgui_draw.cpp','imgui_widgets.cpp','imgui_tables.cpp']],'-o',str(binary)]
    with (O/(name+'-build.log')).open('w') as log:
        subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    remote='/data/local/tmp/wiids-imgui-overlay-test'
    subprocess.run(A+['push',str(binary),remote],capture_output=True,check=True)
    subprocess.run(A+['shell','chmod','700',remote],check=True)
    run=subprocess.run(A+['shell',remote],capture_output=True,text=True)
    (O/(name+'-run.log')).write_text(run.stdout+run.stderr)
    assert (run.returncode==0 and 'PASS ' in run.stdout) if passes else (run.returncode==139),run.stdout+run.stderr
    results.append({'source':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
                    'exit_code':run.returncode,'output':run.stdout.strip(),'expected_pass':passes})
    print(name,run.returncode,run.stdout.strip(),flush=True)
subprocess.run(A+['shell','rm','-f','/data/local/tmp/wiids-imgui-overlay-test'],check=True)
(O/'verification.json').write_text(json.dumps({'status':'PASS','actual_imgui':str(I),'cases':results},indent=2)+'\n')
