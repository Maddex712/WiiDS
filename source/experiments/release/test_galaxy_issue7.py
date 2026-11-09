"""Execute issue #7 regressions against the production C++ control flow on Windows.

No game or device is opened. WPAD is compiled whole; MobileApp's shake block and
SaveManager's request/completion methods are extracted without rewriting them.
Save filesystem callbacks are controlled fakes, not a real NAND validation.
"""
from pathlib import Path
import argparse
import json
import hashlib
import os
import re
import subprocess

R = Path(__file__).resolve().parents[2]
O = R / 'build/github-bugfix-2026-10-26/controls-save/tests'
O.mkdir(parents=True, exist_ok=True)
ap = argparse.ArgumentParser()
ap.add_argument('--before', action='store_true')
ap.add_argument('--part', choices=['spin', 'save', 'status', 'copy', 'directory', 'toast'], required=True)
ap.add_argument('--arm64', action='store_true', help='Compile an isolated Android runner; never connect or execute it.')
a = ap.parse_args()
if a.arm64:
    if a.part == 'copy':
        ap.error('The file-copy injection fixture models Windows APIs; use its host execution.')
    O = O / 'arm64'
    O.mkdir(parents=True, exist_ok=True)
base = R / 'build/github-bugfix-2026-10-26/controls-save/before' if a.before else R
W = R / 'reference/galaxy-android'
S = base / 'reference/galaxy-android'

def method(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    for end in range(brace + 1, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if depth == 0:
            return text[start:end+1]
    raise AssertionError(signature)

common = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdint.h>
#include <cmath>
#include <cerrno>
static int checks=0, failures=0;
void check(bool value,const char* message){++checks;if(!value){++failures;printf("FAIL: %s\n",message);}}
'''
extra = []
if a.part == 'spin':
    text = (S/'platform/mobile/core/MobileApp.cpp').read_text()
    start = text.index('        if (mSpinFrames > 0) {')
    end = text.index('        updateInputState();', start)
    block = text[start:end]
    fallback = 'extern "C" void port_input_spin(int,int) {}\n' if a.before else ''
    fixture = common + r'''
#include "port/input.h"
#include "revolution/kpad.h"
extern "C" int64_t port_host_time_ns(){return 10000000000LL;}
extern "C" int port_skip_inject_a(){return -1;}
extern "C" void port_input_spin(int,int);
int mSpinFrames=0;
PortPadState mPad{};
''' + fallback + '\nvoid tick(){\n' + block + r'''
 port_input_set(0,&mPad);
}
int main(){
 mPad.connected=1;port_input_set(0,&mPad);
 // One button press, then 20 fast render-loop iterations before the next guest read.
 mSpinFrames=6;for(int i=0;i<20;i++)tick();
 KPADStatus s{};
 for(int i=0;i<6;i++){
   check(KPADRead(0,&s,1)==1,"connected read");
   float expected=(i&1)?2.5f:-2.5f;
   check(s.acc.x==expected && s.acc.y==expected-1.f,"all six shake samples reach guest");
   for(int j=0;j<20;j++)tick();
 }
 KPADRead(0,&s,1);check(s.acc.x==0 && s.acc.y==-1,"shake ends after six guest samples");
 // A slower producer likewise cannot shorten the shake.
 mSpinFrames=6;tick();
 for(int i=0;i<6;i++){KPADRead(0,&s,1);check(std::fabs(s.acc.x)==2.5f,"slow producer retains six samples");}
 KPADRead(0,&s,1);check(s.acc.x==0,"slow producer no stuck shake");
 // Repeated touch requests refresh duration without resetting waveform polarity.
 mSpinFrames=6;tick();KPADRead(0,&s,1);float previous=s.acc.x;
 for(int i=0;i<20;i++){mSpinFrames=6;tick();KPADRead(0,&s,1);check(s.acc.x==-previous,"repeated requests alternate");previous=s.acc.x;}
 port_input_spin(0,0);KPADRead(0,&s,1);check(s.acc.x==0,"pause/menu cancellation");
 mSpinFrames=6;tick();mPad.connected=0;port_input_set(0,&mPad);mPad.connected=1;port_input_set(0,&mPad);
 KPADRead(0,&s,1);check(s.acc.x==0,"disconnect clears pending shake");
 mSpinFrames=6;tick();KPADReset();KPADRead(0,&s,1);check(s.acc.x==0,"KPAD reset clears pending shake");
 port_input_spin(-1,6);port_input_spin(4,6);check(KPADRead(-1,&s,1)==0,"invalid channel is ignored");
 printf("%s: %d production spin/WPAD checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
'''
    extra = [str(S/'platform/src/input/wpad.cpp')]
elif a.part == 'save':
    text = (S/'platform/mobile/ui/SaveManager.cpp').read_text()
    methods = '\n'.join(method(text,sig) for sig in [
        'std::string SaveManager::triggerSaveAnywhere(',
        'std::string SaveManager::triggerSaveSlot(',
        'bool SaveManager::update('])
    header = (S/'platform/mobile/ui/SaveManager.h').read_text().replace('#pragma once','')
    fixture = common + header + r'''
static uint64_t generation=1;
static bool saving=false, instant=false, copyOk=true, valid=true;
enum {PORT_QUICK_SAVE_IDLE,PORT_QUICK_SAVE_REQUESTED,PORT_QUICK_SAVE_WRITING,PORT_QUICK_SAVE_SUCCEEDED,PORT_QUICK_SAVE_FAILED,PORT_QUICK_SAVE_REJECTED};
static int quickStatus=0,copies=0,triggers=0,renames=0,failRename=0,failCopy=0;
static bool previousSlot=false,failStaging=false;
struct stat{int unused;};
int stat(const char*,struct stat*){if(previousSlot)return 0;errno=ENOENT;return -1;}
static int64_t now=1000000000LL;
extern "C" int64_t port_host_time_ns(){return now;}
extern "C" uint64_t port_get_save_generation(){return generation;}
extern "C" int port_is_save_in_progress(){return saving;}
extern "C" int port_get_quick_save_status(){return quickStatus;}
extern "C" void port_trigger_quick_save(){triggers++;quickStatus=PORT_QUICK_SAVE_REQUESTED;if(instant){generation++;quickStatus=PORT_QUICK_SAVE_SUCCEEDED;}}
extern "C" void port_android_vibrate(int){}
std::string port_profile_data_path(const char* suffix){return std::string("/profile")+suffix;}
int mkdir(const char* path,int){return failStaging && strstr(path,"_staging")?-1:0;}
int rename(const char*,const char*){renames++;return failRename==renames?-1:0;}
#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)
namespace mobile_ui {
SaveManager::SaveManager()=default;
bool SaveManager::copyDirVerified(const std::string&,const std::string&){copies++;return copyOk && copies!=failCopy;}
bool SaveManager::validateSaveDir(const std::string&){return valid;}
''' + methods + r'''
}
void reset(){generation=1;saving=false;instant=false;copyOk=true;valid=true;copies=triggers=renames=failRename=failCopy=quickStatus=0;previousSlot=failStaging=false;now=1000000000LL;}
void commit(){generation++;quickStatus=PORT_QUICK_SAVE_SUCCEEDED;}
int main(){using mobile_ui::SaveManager;
 reset();SaveManager pending;pending.setSavePath("/nand/settings.ini");pending.triggerSaveAnywhere(false);
 std::string toast;
 for(int i=0;i<1000;i++){now+=1000000;check(!pending.update(toast),"fast UI ticks cannot certify an uncommitted save");}
 check(copies==0,"no stale snapshot before game commit");
 commit();check(pending.update(toast),"successful game commit completes request");check(copies==1,"snapshot copied after commit");
 check(!pending.update(toast),"completed save emitted once");
 reset();SaveManager stalled;stalled.setSavePath("/nand/settings.ini");stalled.triggerSaveAnywhere(false);now+=16000000000LL;
 check(stalled.update(toast),"stalled request reports timeout");check(copies==0,"timeout does not copy stale NAND");
 check(toast.find("Verified")==std::string::npos && toast.find("Saved")==std::string::npos,"timeout never claims save success");
 reset();SaveManager failed;failed.setSavePath("/nand/settings.ini");failed.triggerSaveAnywhere(false);commit();copyOk=false;
 check(failed.update(toast),"snapshot failure reports result");check(toast.find("Verified")==std::string::npos,"failed copy never claims verified");
 reset();SaveManager rapid;rapid.setSavePath("/nand/settings.ini");instant=true;rapid.triggerSaveAnywhere(false);
 check(rapid.update(toast),"commit racing trigger is observed");check(copies==1,"fast commit snapshots once");
 reset();SaveManager slow;slow.setSavePath("/nand/settings.ini");slow.triggerSaveAnywhere(false);saving=true;
 for(int i=0;i<500;i++){now+=1000000;check(!slow.update(toast),"active NAND write never snapshots early");}
 check(copies==0,"no copy during NAND write");commit();saving=false;check(slow.update(toast),"slow commit completes");
 reset();SaveManager bad;bad.setSavePath("settings.ini");bad.triggerSaveAnywhere(false);commit();
 check(bad.update(toast),"bad path reports failure");check(copies==0,"invalid path no copy");
 reset();SaveManager unrelated;unrelated.setSavePath("/nand/settings.ini");unrelated.triggerSaveAnywhere(false);generation++;
 check(!unrelated.update(toast),"unrelated mail/system commit cannot complete Quick Save");check(copies==0,"unrelated commit no snapshot");
 quickStatus=PORT_QUICK_SAVE_REJECTED;check(unrelated.update(toast),"busy game rejection is reported immediately");
 check(copies==0 && toast.find("saved")==std::string::npos,"rejected request never claims saved");
 reset();SaveManager rejected;rejected.setSavePath("/nand/settings.ini");rejected.triggerSaveAnywhere(false);quickStatus=PORT_QUICK_SAVE_FAILED;
 check(rejected.update(toast) && copies==0,"write failure is reported immediately");
 reset();SaveManager duplicate;duplicate.setSavePath("/nand/settings.ini");duplicate.triggerSaveAnywhere(false);duplicate.triggerSaveAnywhere(false);duplicate.triggerSaveSlot(4,false);
 check(triggers==1,"pending request cannot be overwritten");
 for(int scenario=0;scenario<7;scenario++){
  reset();SaveManager slot;slot.setSavePath("/nand/settings.ini");slot.triggerSaveSlot(4,false);commit();
  previousSlot=true;
  if(scenario==1)failRename=1;
  if(scenario==2)failRename=2;
  if(scenario==3)failCopy=2;
  if(scenario==4)failCopy=3;
  if(scenario==5)failStaging=true;
  if(scenario==6)previousSlot=false;
  check(slot.update(toast),"snapshot operation gives result");
  if(scenario==0)check(renames==2 && copies==3,"existing snapshot commits then mirrors");
  if(scenario==1)check(renames==1 && copies==1,"failed previous-slot move preserves old slot");
  if(scenario==2)check(renames==3 && copies==1,"failed replacement rolls back before any mirrors");
  if(scenario==3 || scenario==4)check(toast.find("failed")!=std::string::npos,"mirror failure is not reported as full success");
  if(scenario==5)check(renames==0 && copies==0,"staging failure leaves saves untouched");
  if(scenario==6)check(renames==1 && copies==3,"first snapshot commits without previous-slot rename");
 }
 printf("%s: %d production Quick Save checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
'''
elif a.part == 'status':
    text=(S/'platform/src/port/missing_game.cpp').read_text()
    start=text.index('static int sQuickSaveRequested')
    end=text.index('static int sQuickLoadRequested',start)
    group=text[start:end]
    fallback='int port_get_quick_save_status(){return 0;} void port_reject_quick_save(){}' if a.before else ''
    fixture=common+r'''
enum {PORT_QUICK_SAVE_IDLE,PORT_QUICK_SAVE_REQUESTED,PORT_QUICK_SAVE_WRITING,PORT_QUICK_SAVE_SUCCEEDED,PORT_QUICK_SAVE_FAILED,PORT_QUICK_SAVE_REJECTED};
'''+group+fallback+r'''
int main(){
 port_trigger_quick_save();check(port_get_quick_save_status()==PORT_QUICK_SAVE_REQUESTED,"request identity is pending");
 port_save_notify_completed(1);check(port_get_quick_save_status()==PORT_QUICK_SAVE_REQUESTED,"unrelated system write cannot complete request");
 check(port_check_quick_save_request()==1 && port_check_quick_save_request()==0,"game consumes request once");
 port_reject_quick_save();check(port_get_quick_save_status()==PORT_QUICK_SAVE_REJECTED,"busy/no-file rejection tracked");
 port_save_notify_completed(1);check(port_get_quick_save_status()==PORT_QUICK_SAVE_REJECTED,"later unrelated write cannot turn rejection into success");
 port_trigger_quick_save();port_check_quick_save_request();port_save_notify_started();check(port_get_quick_save_status()==PORT_QUICK_SAVE_WRITING,"accepted request tracks writing");
 port_save_notify_completed(0);check(port_get_quick_save_status()==PORT_QUICK_SAVE_FAILED,"accepted write failure tracked");
 port_trigger_quick_save();port_check_quick_save_request();port_save_notify_started();port_save_notify_completed(1);
 check(port_get_quick_save_status()==PORT_QUICK_SAVE_SUCCEEDED && !port_is_save_in_progress(),"accepted successful write tracked");
 printf("%s: %d production Quick Save status checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
'''
elif a.part == 'toast':
    text=(S/'platform/mobile/ui/UIOverlay.cpp').read_text()
    actual='\n'.join(method(text,sig) for sig in ['void UIOverlay::update(', 'void UIOverlay::showToast('])
    fixture=common+r'''
static int64_t now=1000000000LL;
extern "C" int64_t port_host_time_ns(){return now;}
struct PortPadState{};
namespace mobile_ui {
struct Settings{bool fullTouchMode=false,batterySaver=false;int targetFps=60;};
struct Touch{void update(PortPadState&,int&,bool){}};
struct Perf{void update(int,bool){}};
struct Save{bool finished=false;bool update(std::string& text){if(!finished)return false;finished=false;text="Game progress saved.";return true;}};
class UIOverlay{public:
 Touch mTouch;Perf mPerf;Save mSave;Settings mSettings;
 std::string mToastMessage;float mToastTimer=0;int64_t mToastExpiresNs=0;bool mDirty=false;
 void update(PortPadState&,int&);void showToast(const std::string&);
};
'''+actual+r'''
}
int main(){using mobile_ui::UIOverlay;PortPadState pad;int spin=0;UIOverlay o;
 o.showToast("Saving...");
 for(int i=0;i<1000;i++){now+=1000000;o.update(pad,spin);}
 check(std::fabs(o.mToastTimer-1.8f)<.0001f,"1000 fast UI ticks consume one real second");
 for(int i=0;i<1000;i++)o.update(pad,spin);
 check(std::fabs(o.mToastTimer-1.8f)<.0001f,"unchanged clock never expires a toast");
 now+=1799000000LL;o.update(pad,spin);check(o.mToastTimer>0,"toast remains until its2.8s deadline");
 now+=1000000LL;o.mDirty=false;o.update(pad,spin);check(o.mToastTimer==0 && o.mDirty,"deadline clears toast and redraws");
 o.showToast("Saving...");now+=2799000000LL;o.mSave.finished=true;o.update(pad,spin);
 check(o.mToastMessage=="Game progress saved." && std::fabs(o.mToastTimer-2.8f)<.0001f,"save completion starts a fresh full duration");
 now+=30000000000LL;o.update(pad,spin);check(o.mToastTimer==0,"background interval does not resurrect stale feedback");
 o.showToast("Retry");o.update(pad,spin);check(std::fabs(o.mToastTimer-2.8f)<.0001f,"new toast after resume gets full duration");
 printf("%s: %d production toast timing checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
'''
elif a.part == 'directory':
    text=(S/'platform/mobile/ui/SaveManager.cpp').read_text()
    actual=method(text,'bool SaveManager::copyDirVerified(')
    fixture=common+r'''
struct dirent{const char* d_name;};struct DIR{int n;};struct stat{int st_mode;};
#define S_ISDIR(m) ((m)==2)
#define S_ISREG(m) ((m)==1)
static int fault=0,copies=0;static DIR directory;static dirent entry={"GameData.bin"};
int mkdir(const char*,int){if(fault==1){errno=EACCES;return -1;}if(fault==8){errno=EEXIST;return -1;}return 0;}
DIR* opendir(const char*){directory.n=0;return fault==2?nullptr:&directory;}
dirent* readdir(DIR* d){if(d->n++==0)return &entry;if(fault==3)errno=EIO;return nullptr;}
int stat(const char*,struct stat* s){if(fault==4)return -1;s->st_mode=1;return 0;}
int closedir(DIR*){return fault==5?-1:0;}
namespace mobile_ui {class SaveManager{public:static bool copyDirVerified(const std::string&,const std::string&);static bool copyFileVerified(const std::string&,const std::string&){copies++;return fault!=6;}};
'''+actual+r'''
}
int main(){
 for(fault=0;fault<=8;fault++){
  copies=0;bool ok=mobile_ui::SaveManager::copyDirVerified("/source","/backup");
  check(ok==(fault==0 || fault==7 || fault==8),"directory traversal/creation/stat/close/copy errors must fail backup");
  if(fault==1 || fault==2 || fault==4)check(copies==0,"unreadable source or unavailable destination does not copy");
 }
 printf("%s: %d production snapshot directory checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
'''
else:
    text=(S/'platform/mobile/ui/SaveManager.cpp').read_text()
    actual=method(text,'bool SaveManager::copyFileVerified(')
    # Wrap standard I/O only to inject failures after a real byte-for-byte copy.
    fixture=common+r'''
#include <sys/stat.h>
#include <io.h>
#include <fstream>
static int fault=0,renames=0,unlinks=0;
int testFlush(FILE* f){int result=fflush(f);return fault==1?-1:result;}
int testSync(int fd){int result=_commit(fd);return fault==2?-1:result;}
int testClose(FILE* f){int result=fclose(f);return fault==3?-1:result;}
int testError(FILE* f){return fault==4?1:ferror(f);}
// Windows rename refuses an existing destination; model Android's POSIX replacement.
int testRename(const char* a,const char* b){renames++;_unlink(b);return rename(a,b);}
int testUnlink(const char* p){unlinks++;return _unlink(p);}
namespace mobile_ui {class SaveManager {public:static bool copyFileVerified(const std::string&,const std::string&);};
#define fflush testFlush
#define fsync testSync
#define fclose testClose
#define ferror testError
#define rename testRename
#define unlink testUnlink
'''+actual+r'''
#undef fflush
#undef fclose
#undef ferror
#undef rename
#undef unlink
}
int main(){
 const char* src="issue7-copy-source.bin";const char* dst="issue7-copy-destination.bin";
 for(fault=0;fault<=4;fault++){
  _unlink(dst);_unlink("issue7-copy-destination.bin.tmp");renames=unlinks=0;
  if(fault){FILE* old=fopen(dst,"wb");fputs("previous-snapshot",old);fclose(old);}
  FILE* f=fopen(src,"wb");for(int i=0;i<4096;i++)fputc(i%251,f);fclose(f);
  bool ok=mobile_ui::SaveManager::copyFileVerified(src,dst);
  check(ok==(fault==0),"copy success requires read/flush/fsync/close success");
  check(renames==(fault==0?1:0),"failed I/O never renames over destination");
  if(fault==0){std::ifstream in(dst,std::ios::binary);std::string data((std::istreambuf_iterator<char>(in)),{});check(data.size()==4096,"successful copy length");for(size_t i=0;i<data.size();i++)check((unsigned char)data[i]==i%251,"successful copy exact bytes");}
  else {check(unlinks==1,"failed staged copy cleaned up");std::ifstream old(dst,std::ios::binary);std::string data((std::istreambuf_iterator<char>(old)),{});check(data=="previous-snapshot","I/O failure preserves existing backup bytes");}
 }
 _unlink(src);_unlink(dst);_unlink("issue7-copy-destination.bin.tmp");
 printf("%s: %d production snapshot I/O checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
'''

label = a.part + ('-before' if a.before else '-fixed')
cpp = O/(label+'.cpp')
cpp.write_text(fixture,encoding='utf-8')
exe = O/(label+'.exe')
if a.arm64:
    compiler=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang++.cmd'
    binary=O/label
    cmd=[str(compiler),'-std=c++17','-O1','-static-libstdc++','-pthread',
        '-I'+str(S/'platform/include'),'-I'+str(W/'platform/include'),
        '-I'+str(W/'decomp/libs/RVL_SDK/include'),str(cpp),*extra,'-o',str(binary)]
    build=subprocess.run(cmd,capture_output=True,text=True)
    assert build.returncode==0,build.stdout+build.stderr
    data=binary.read_bytes()
    assert data[:5]==b'\x7fELF\x02' and int.from_bytes(data[18:20],'little')==183
    receipt={'status':'COMPILED_NOT_EXECUTED','part':a.part,'baseline_expected_failure':a.before,
        'binary':str(binary),'binary_sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),
        'fixture_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest(),
        'scope':'Standalone Android ARM64 source regression; deterministic fake input/clock/NAND callbacks. No owner save paths accessed, no device connection.'}
    (O/(label+'-compile.json')).write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))
    raise SystemExit(0)
compiler=R/'build/github-bugfix-2026-10-26/tooling/zig-x86_64-windows-0.15.2/zig.exe'
cmd=[str(compiler),'c++','-std=c++17','-O1',
     '-I'+str(S/'platform/include'),'-I'+str(W/'platform/include'),
     '-I'+str(W/'decomp/libs/RVL_SDK/include'),str(cpp),*extra,'-o',str(exe)]
build=subprocess.run(cmd,capture_output=True,text=True)
assert build.returncode==0,build.stdout+build.stderr
run_dir=O/(label+'-run')
run_dir.mkdir(exist_ok=True)
run=subprocess.run([str(exe)],cwd=run_dir,capture_output=True,text=True)
print(run.stdout[-3000:])
if a.before:
    assert run.returncode!=0 and 'FAIL:' in run.stdout
else:
    assert run.returncode==0,run.stdout+run.stderr
(O/(label+'.json')).write_text(json.dumps({'part':a.part,'baseline_expected_failure':a.before,
    'exit_code':run.returncode,'output':run.stdout,'scope':'Production C++ host regression with controlled callbacks; no physical game/device test'},indent=2)+'\n')
