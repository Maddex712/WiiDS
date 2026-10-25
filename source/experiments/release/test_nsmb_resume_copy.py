"""Run the actual NSMB display-copy wrapper against frame-start success/failure.

Uses a freestanding Windows fixture, so no device or game needs to be launched.
"""
from pathlib import Path
import argparse
import subprocess
import json

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/feature-1.1/resume/tests'
OUT.mkdir(parents=True, exist_ok=True)
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT/'reference/nsmbw-android-e1/runtime/src/hle/gx/gx_copy.cpp')
parser.add_argument('--expect-failure', action='store_true')
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')
start = source.index('extern "C" void GX__CopyDisp_')
brace = source.index('{', start); depth = 1
for end in range(brace+1, len(source)):
    depth += (source[end] == '{') - (source[end] == '}')
    if depth == 0: break
actual = source[start:end+1]
tex_start = source.index('extern "C" void GX__CopyTex_')
tex_end = source.index('    const uint16_t rawSrcLeft', tex_start)
actual += '\n' + source[tex_start:tex_end] + '    ++textureCopies;\n}\n'
fixture = r'''
extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned int);
extern "C" __declspec(dllimport) void* __stdcall GetStdHandle(unsigned int);
extern "C" __declspec(dllimport) int __stdcall WriteFile(void*,const void*,unsigned int,unsigned int*,void*);
namespace std { constexpr int memory_order_acquire=0; }
struct Flag { bool value=false; bool load(int)const{return value;} } g_auroraFrameActive;
using uint32_t=unsigned int; using GXBool=int;
int g_gxFrameCount=0,attempts=0,drains=0,copies=0,invalid=0,ready=0,waits=0,draws=0,presents=0,checks=0,textureCopies=0;
bool retrySucceeds=false;
void EnsureAuroraFrameActive(){attempts++;if(retrySucceeds)g_auroraFrameActive.value=true;}
void GXDrawDone(){drains++;}
void* GuestToHostPtr(uint32_t p){return reinterpret_cast<void*>(static_cast<unsigned long long>(p));}
void GXCopyDisp(void*,GXBool){copies++;if(!g_auroraFrameActive.value)invalid++;}
void VI_HLE_SetXfbReady(uint32_t){ready++;}
void aurora_wait_for_frame_worker(){waits++;}
namespace settings_overlay{void Draw(){draws++;}}
void VI_HLE_PresentFrame(bool,bool){presents++;}
''' + actual + r'''
void say(const char* text){unsigned int n=0,written=0;while(text[n])n++;WriteFile(GetStdHandle((unsigned int)-11),text,n,&written,nullptr);}
void check(bool ok){checks++;if(!ok){say("FAIL inactive frame reached display-copy GPU/presentation path");ExitProcess(1);}}
void reset(bool active,bool success){g_auroraFrameActive.value=active;retrySucceeds=success;attempts=drains=copies=invalid=ready=waits=draws=presents=g_gxFrameCount=textureCopies=0;}
extern "C" void entry(){
 reset(false,false);GX__CopyDisp_8016fc38(0x80000000u,1);
 check(attempts==1);check(copies==0);check(invalid==0);check(drains==0);check(ready==0);check(waits==0);check(draws==0);check(presents==0);check(g_gxFrameCount==0);
 reset(false,true);GX__CopyDisp_8016fc38(0x80000000u,1);
 check(copies==1&&invalid==0);check(drains==1);check(ready==1);check(waits==1);check(draws==1);check(presents==1);check(g_gxFrameCount==1);
 reset(true,false);GX__CopyDisp_8016fc38(0x80000000u,0);
 check(copies==1&&invalid==0);check(presents==1);check(g_gxFrameCount==1);
 reset(false,false);for(int i=0;i<5000;i++)GX__CopyDisp_8016fc38(0x80000000u,1);
 check(copies==0&&presents==0&&g_gxFrameCount==0);
 retrySucceeds=true;GX__CopyDisp_8016fc38(0x80000000u,1);
 check(copies==1&&presents==1&&g_gxFrameCount==1);
 reset(false,false);GX__CopyTex_8016fd74(0x80000000u,1);check(textureCopies==0);
 retrySucceeds=true;GX__CopyTex_8016fd74(0x80000000u,1);check(textureCopies==1);
 GX__CopyTex_8016fd74(0x80000000u,1);check(textureCopies==2);
 say("PASS 24 production NSMB display-copy/texture-entry resume checks");ExitProcess(0);
}
'''
cpp=OUT/('before.cpp' if args.expect_failure else 'test.cpp')
exe=cpp.with_suffix('.exe'); cpp.write_text(fixture,encoding='utf-8')
clang=ROOT/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
kernel=Path('C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/um/x64/kernel32.lib')
obj=cpp.with_suffix('.obj')
subprocess.run([str(clang),'--target=x86_64-pc-windows-msvc','-fno-exceptions','-fno-rtti',
    '-fno-stack-protector','-O1','-c',str(cpp),'-o',str(obj)],check=True)
subprocess.run([str(clang.parent/'ld.lld.exe'),'-flavor','link','/entry:entry','/subsystem:console',
    '/nodefaultlib',str(obj),str(kernel),'/out:'+str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True)
print(run.stdout.strip())
if args.expect_failure:
    assert run.returncode != 0 and 'FAIL inactive frame' in run.stdout,run.stdout+run.stderr
else:
    assert run.returncode == 0,run.stdout+run.stderr
(OUT/('baseline.json' if args.expect_failure else 'verification.json')).write_text(json.dumps({
    'expected_baseline_failure':args.expect_failure,'exit_code':run.returncode,'result':run.stdout.strip(),
    'scope':'Actual display-copy wrapper and texture-copy entry preamble with a controlled frame-start timeout; not the original device crash or real GPU',
    'source':str(args.source)},indent=2)+'\n',encoding='utf-8')
