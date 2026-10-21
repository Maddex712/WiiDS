"""Exercise the actual native HUD helpers against owner-disc pane bounds; no device input."""
from pathlib import Path
import argparse,json,runpy,struct,subprocess,re,shutil
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'reference/sms-thor'
OUT=ROOT/'build/sunshine-hud-fit'
LLVM=ROOT/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'

def function(text,signature):
 start=text.index(signature);brace=text.index('{',start);depth=1
 for end in range(brace+1,len(text)):
  depth+=(text[end]=='{')-(text[end]=='}')
  if not depth:return text[start:end+1]
 raise ValueError(signature)

def verify_patch_chain():
 out=OUT/'patch-proof';out.mkdir(parents=True,exist_ok=True)
 paths=['src/GC2D/GCConsole2.cpp','libs/JSystem/src/J2D/J2DPane.cpp']
 for rel in paths:
  target=out/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(SOURCE/'decomp'/rel,target)
 applied=[]
 for patch in sorted((SOURCE/'decomp-patches').glob('*.patch')):
  sections=re.split(rb'(?m)(?=^--- a/)',patch.read_bytes().replace(b'\r\n',b'\n'))
  selected=[part for part in sections if any(part.startswith(('--- a/'+rel+'\n').encode()) or part.startswith(('--- a/'+rel+'\t').encode()) for rel in paths)]
  if not selected:continue
  part=out/'selected.patch';part.write_bytes(b''.join(selected))
  run=subprocess.run(['C:/Program Files/Git/usr/bin/patch.exe','-p1','--quiet','--force','-d',str(out),'-i',str(part)],capture_output=True,text=True)
  if run.returncode:raise RuntimeError(patch.name+':'+run.stdout+run.stderr)
  applied.append(patch.name)
 text=(out/paths[0]).read_text(encoding='utf-8')
 assert 'port_thor_lives_shift(thorLives->getBounds().x1,' in text
 assert 'thorLife->add(-thorLifeDx, 0);' in text
 text=(out/paths[1]).read_text(encoding='utf-8')
 assert 'port_thor_hud_anchor_right(mUserInfoTag, port_thor_hud_split() != 0)' in text
 (out/'verification.json').write_text(json.dumps({'patch_chain_passed':True,'target_files':paths,'patches_applied':applied,'no_native_build_or_device_input':True},indent=2)+'\n')
 print('Full patch chain passed:',len(applied),'patches across two actual game source files.')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--baseline',action='store_true');a=ap.parse_args()
 OUT.mkdir(parents=True,exist_ok=True)
 if not a.baseline:verify_patch_chain()
 m=runpy.run_path(str(ROOT/'experiments/sunshine-thor/Extract-ThorHud.py'))
 archive=m['rarc'](m['yaz0']((ROOT/'build/sunshine-thor/extracted/files/data/game_6.szs').read_bytes()))
 blo=next(b for name,b in archive.items() if name.endswith('/standard_1.blo'))
 panes={}
 for i in range(len(blo)-24):
  if blo[i:i+4] in (b'PAN1',b'PIC1'):
   panes[blo[i+12:i+16]]=struct.unpack_from('>4h',blo,i+16)
 assert panes[b'\0m_0']==(223,74,10,10) and panes[b'm_tx']==(-12,-34,72,34)
 assert panes[b'\0l_0']==(500,50,10,10)
 snapshot=(SOURCE/'android/thor_snapshot.cpp').read_text()
 render=(SOURCE/'platform/gx/src/gx_render.cpp').read_text()
 bound=(SOURCE/'decomp/src/GC2D/BoundPane.cpp').read_text()
 changed='int contentLeft' in snapshot
 header=SOURCE/'src/port_include/port_thor_hud_layout.h'
 includes='#include "'+header.as_posix()+'"\n' if changed else ''
 life=function(snapshot,'extern "C" int port_thor_life_shift') if changed else 'int port_thor_life_shift(){return 0;}'
 call='port_thor_lives_shift(223,-12)' if changed else 'port_thor_lives_shift(223)'
 anchor='port_thor_hud_anchor_right(0x006c5f30u, true)' if changed else 'false'
 program=r'''
extern "C" __declspec(dllimport) void ExitProcess(unsigned);
extern "C" __declspec(dllimport) void* GetStdHandle(unsigned);
extern "C" __declspec(dllimport) int WriteFile(void*,const void*,unsigned,unsigned*,void*);
extern "C" int _fltused=0;
static int edges;
int strcmp(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return (unsigned char)*a-(unsigned char)*b;}
const char* getenv(const char* name){return edges&&!strcmp(name,"SMS_WIDESCREEN_HUD")?"edges":0;}
extern "C" void* memset(void*p,int x,unsigned long long n){char*q=(char*)p;while(n--)*q++=(char)x;return p;}
extern "C" void* memcpy(void*p,const void*s,unsigned long long n){char*q=(char*)p;const char*t=(const char*)s;while(n--)*q++=*t++;return p;}
extern "C" float port_widescreen=1.33333333333f;
enum {EFB_W=640};static int s_ox=107;
typedef float f32;typedef short s16;
struct JUTPoint {int x,y;JUTPoint(int X=0,int Y=0):x(X),y(Y){} void set(int X,int Y){x=X;y=Y;}};
struct TBoundPane {static void makeNewPosition(f32,JUTPoint&,JUTPoint&,JUTPoint&,JUTPoint&);};
'''+includes+function(snapshot,'extern "C" int port_thor_lives_shift')+'\n'+life+'\n'+function(render,'static float hudOffset')+'\n'+function(bound,'void TBoundPane::makeNewPosition')+r'''
static int checks,failures;
static void check(bool pass){++checks;if(!pass)++failures;}
static void say(const char* s){unsigned n=0,w=0;while(s[n])++n;WriteFile(GetStdHandle((unsigned)-11),s,n,&w,0);}
extern "C" void mainCRTStartup(){
 for(int mode=0;mode<2;++mode){edges=mode;
  int dx='''+call+r''';
  int root=223+dx;
  float mapped=mode?hudOffset((float)root+5):107;
  check(root-12+mapped>=6 && root-12+mapped<=7);
  JUTPoint start(-150,180),control(-10,10),end(0,0),point;
  for(int i=0;i<=40;++i){
   TBoundPane::makeNewPosition((float)i/40,point,start,control,end);
   float center=500+point.x+5;
   float anchor='''+anchor+r'''?640:center;
   float translation=port_thor_life_shift()+(mode?hudOffset(anchor):107);
   check(translation==214);
   float physical=500+point.x+translation;
   check(physical-(500+point.x)==214);
  }
 }
 port_widescreen=1;edges=0;s_ox=0;
 int dx='''+call+r''';check(223+dx-12==6);check(port_thor_life_shift()==0);
 say(failures?"HUD anchor checks FAILED\n":"HUD anchor checks PASSED\n");
 ExitProcess(failures?1:0);
}
'''
 tag='before' if a.baseline else 'after';cpp=OUT/(tag+'.cpp');obj=OUT/(tag+'.obj');exe=OUT/(tag+'.exe')
 if a.baseline and changed:
  # Re-run the untouched actual-source harness captured before this repair.
  if not cpp.is_file():raise ValueError('No pre-fix baseline was captured in this workbench')
  program=cpp.read_text();changed=False
 cpp.write_text(program)
 subprocess.run([str(LLVM/'clang++.exe'),'--target=x86_64-pc-windows-msvc','-std=c++17','-O2','-fno-exceptions','-fno-rtti','-fno-builtin','-fno-stack-protector','-c',str(cpp),'-o',str(obj)],check=True)
 kernel=Path('C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/um/x64/kernel32.lib')
 subprocess.run([str(LLVM/'ld.lld.exe'),'-flavor','link','/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/out:'+str(exe),str(obj),str(kernel)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True);print(result.stdout,end='')
 receipt={'baseline':a.baseline,'native_source_changed':changed,'exit_code':result.returncode,'actual_source_checks':168,'asset_panes':{k.hex():list(v) for k,v in panes.items() if k in (b'\0m_0',b'm_tx',b'\0l_0')},'live_game_tested':False}
 (OUT/(tag+'-verification.json')).write_text(json.dumps(receipt,indent=2)+'\n')
 if a.baseline:assert result.returncode==1,'Baseline must reproduce clipping / inconsistent life anchor'
 else:assert result.returncode==0,'Fixed native helper checks must pass'

if __name__=='__main__':main()
