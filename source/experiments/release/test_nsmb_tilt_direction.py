"""Exercise actual NSMB virtual motion, KPAD filtering and translated E1 angle code.

No game process or device access. Generated game bodies stay in ignored build/.
The expected sign is the existing screen-direction contract recorded by the
older port's physical A/D inversion investigation, not a new control mapping.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TASK = ROOT / 'build/discord-bugfix-2026-10-26/tilt'


def body(text, start):
    at = text.index(start)
    begin = text.index('{', at)
    depth = 1
    end = begin + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[at:end]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--before', action='store_true', help='Use preserved unmodified motion source')
    parser.add_argument('--arm64', action='store_true', help='Compile only; root runs isolated fixture')
    parser.add_argument('--variant', choices=['e1', 'msmbwii'], default='e1')
    a = parser.parse_args()
    base = ROOT / ('reference/nsmbw-android-' + a.variant)
    native = base / 'runtime/src/virtual_remote.cpp'
    source = TASK / 'before' / native.relative_to(ROOT) if a.before else native
    kpad = base / 'runtime/src/hle/input/kpad.cpp'
    shard = ROOT / 'reference/nsmbw-android-e1/generated/build_shards/base_common/shard_e4647d2af053b3c9d09e667b.cpp'
    motion, filtering, guest = source.read_text(), kpad.read_text(), shard.read_text()
    # Preserve both actual source bodies; the fixture supplies only IO/memory seams.
    constants = motion[motion.index('constexpr uint32_t kWpadLeft'):motion.index('std::mutex g_mutex')]
    methods = '\n'.join(body(motion, s) for s in [
        'Intent KeyboardIntent()', 'Intent TouchIntent()', 'Intent PadIntent(',
        'void ApplyMotion(', 'void SetTouchState('])
    channel = body(filtering, 'struct ChannelState') + ';\n'
    channel += re.search(r'constexpr int kReportsPerRead = [^;]+;', filtering)[0] + '\n'
    channel += body(filtering, 'void UpdateAccVertical(')
    converter = body(guest, 'extern "C" void func_800B61A0(')
    getter = body(guest, 'extern "C" void func_800B5C50(')
    fixture = r'''
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <dolphin/pad.h>
#include "virtual_remote.h"
#include "keybinds.h"
#include "isa/ppc_isa_config.h"
#include "isa/ppc_isa_int.h"
#include "isa/ppc_isa_cr.h"
#include "isa/ppc_isa_float.h"
namespace Keybinds {
bool keys[kActionCount]{};
bool Held(int action) { return keys[action]; }
bool Suppressed() { return false; }
}
void* SDL_GetKeyboardFocus() { return reinterpret_cast<void*>(1); }
namespace VirtualRemote {
@CONSTANTS@
@METHODS@
}
@CHANNEL@
// Logical guest-memory IO: production PPC arithmetic and complete generated
// converter/getter run unchanged. No entire guest address space is required.
namespace MemoryInline {
std::unordered_map<uint32_t,uint8_t> bytes;
uint32_t FlatRead32(uint32_t at) {
    return (uint32_t(bytes[at])<<24)|(uint32_t(bytes[at+1])<<16)|(uint32_t(bytes[at+2])<<8)|bytes[at+3];
}
uint16_t FlatRead16(uint32_t at) { return uint16_t((bytes[at]<<8)|bytes[at+1]); }
void FlatWriteRam32(uint32_t at,uint32_t v) { for(int i=0;i<4;++i)bytes[at+i]=uint8_t(v>>(24-8*i)); }
void FlatWrite16(uint32_t at,uint16_t v) { bytes[at]=uint8_t(v>>8);bytes[at+1]=uint8_t(v); }
float FlatReadFloat32(uint32_t at) { auto v=FlatRead32(at);float f;std::memcpy(&f,&v,4);return f; }
void Float32(uint32_t at,float f) { uint32_t v;std::memcpy(&v,&f,4);FlatWriteRam32(at,v); }
void FlatWriteRamFloat64(uint32_t at,double f) { uint64_t v;std::memcpy(&v,&f,8);FlatWriteRam32(at,uint32_t(v>>32));FlatWriteRam32(at+4,uint32_t(v)); }
}
int unexpectedExtensionCalls=0;
void func_8015F200(CpuContext*) { ++unexpectedExtensionCalls; }
#define MKW_STATIC_TRANSLATED_CALL(address,name,ctx) name(ctx)
@CONVERTER@
@GETTER@
int checks=0, failures=0;
void check(bool ok,const char* why) { ++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);} }
int angleFor(const ChannelState& filter,const WiiRemoteInput::KpadSample& sample) {
    constexpr uint32_t core=0x80010000,sda=0x80430000;
    CpuContext ctx{};ctx.gpr[1]=0x80030000;ctx.gpr[2]=sda;ctx.gpr[3]=core;
    MemoryInline::FlatWriteRam32(core+4,0); // player one
    MemoryInline::FlatWriteRam32(core+8,0); // bare virtual Wii Remote
    MemoryInline::FlatWriteRam32(0x803741D0,0); // no replay override
    MemoryInline::Float32(sda-27328,16384.0f);
    MemoryInline::Float32(core+60,sample.acc[2]);
    MemoryInline::Float32(core+80,filter.accVertical[1]);
    func_800B61A0(&ctx);
    ctx.gpr[3]=core;func_800B5C50(&ctx);
    return static_cast<int16_t>(ctx.gpr[3]>>16);
}
struct Pipeline {
    VirtualRemote::Motion motion{};ChannelState filter{};WiiRemoteInput::KpadSample sample{};
    int tick(VirtualRemote::Intent intent) {
        VirtualRemote::ApplyMotion(motion,intent,sample);
        UpdateAccVertical(filter,sample.acc);
        return angleFor(filter,sample);
    }
};
int main() {
    using namespace VirtualRemote;
    for(int source=0;source<3;++source)for(int sign:{-1,1}) {
        Intent intent;
        if(source==0){SetTouchState(0,false,float(sign));intent=TouchIntent();}
        if(source==1){PADStatus pad{};pad.substickX=sign*127;intent=PadIntent(pad);}
        if(source==2){Keybinds::keys[Keybinds::kTiltRight]=sign>0;Keybinds::keys[Keybinds::kTiltLeft]=sign<0;intent=KeyboardIntent();}
        check(intent.tilt==sign,"touch/stick/key preserves requested screen direction");
        Pipeline p;int angle=0;
        for(int i=0;i<120;++i)angle=p.tick(intent);
        check(sign*angle>11000,"right clockwise gives positive game angle; left gives negative");
        check(sign*p.sample.acc[2]<-.70f,"virtual KPAD acceleration agrees with direction contract");
        check(std::fabs(p.sample.acc[0])<.001f&&p.sample.acc[1]<-.70f,"other acceleration axes preserve resting attitude");
        check(std::fabs(std::hypot(p.filter.accVertical[0],p.filter.accVertical[1])-1)<.0001f,"filtered attitude remains normalized");
        intent.tilt=0;for(int i=0;i<180;++i)angle=p.tick(intent);
        check(std::abs(angle)<=1&&p.motion.tilt==0,"release returns game angle to neutral");
    }
    Keybinds::keys[Keybinds::kTiltRight]=Keybinds::keys[Keybinds::kTiltLeft]=false;
    for(float amount:{.1f,.25f,.5f,1.0f}) {
        Pipeline left,right;int la=0,ra=0;
        for(int i=0;i<120;++i){la=left.tick({0,false,-amount});ra=right.tick({0,false,amount});}
        check(ra>0&&la<0,"all tilt magnitudes use correct screen direction");
        check(std::abs(ra+la)<=1,"left/right tilt remains symmetric");
    }
    Pipeline level;for(int i=0;i<10;++i)check(level.tick({})==0,"neutral remote stays level");
    Pipeline shaking;int shakeEdges=0;float previous=-1;
    for(int i=0;i<14;++i){shaking.tick({0,i==0,0});if(std::fabs(shaking.sample.acc[1]-previous)>=.28f)++shakeEdges;previous=shaking.sample.acc[1];}
    check(shakeEdges>=4,"one shake request still spans enough game reads");
    check(shaking.motion.shakeFramesLeft==0&&shaking.sample.acc[1]==-1,"shake burst expires to rest");
    check(angleFor(shaking.filter,shaking.sample)==0,"level shake does not manufacture tilt");
    Pipeline combined;for(int i=0;i<120;++i)combined.tick({0,false,1});
    combined.tick({0,true,1});check(combined.sample.acc[2]<0,"shake does not reverse held tilt");
    SetTouchState(0,false,std::numeric_limits<float>::quiet_NaN());check(TouchIntent().tilt==0,"invalid touch tilt remains neutral");
    PADStatus centre{};centre.substickX=20;check(PadIntent(centre).tilt==0,"stick dead zone unchanged");
    check(unexpectedExtensionCalls==0,"fixture exercised actual no-extension converter branch");
    std::printf("%s: %d actual-source tilt checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
'''
    for key, value in [('CONSTANTS',constants),('METHODS',methods),('CHANNEL',channel),('CONVERTER',converter),('GETTER',getter)]:
        fixture = fixture.replace('@'+key+'@',value)
    out = TASK / ('arm64' if a.arm64 else 'host') / a.variant
    out.mkdir(parents=True,exist_ok=True)
    label = 'before' if a.before else 'fixed'
    cpp = out / (label+'.cpp')
    cpp.write_text(fixture,encoding='utf-8')
    binary = out / (label if a.arm64 else label+'.exe')
    compiler = ROOT / ('.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang++.cmd' if a.arm64 else 'build/github-bugfix-2026-10-26/tooling/zig-x86_64-windows-0.15.2/zig.exe')
    cmd = [str(compiler)] + ([] if a.arm64 else ['c++']) + ['-std=c++20','-O1','-DTARGET_PC',str(cpp),'-o',str(binary),'-I'+str(base/'runtime/include'),'-I'+str(base/'aurora-main/include')]
    if a.arm64: cmd += ['-static-libstdc++','-pthread']
    built = subprocess.run(cmd,capture_output=True,text=True)
    assert built.returncode==0,built.stdout+built.stderr
    report = dict(variant=a.variant,expected_old_failure=a.before,binary_sha256=sha(binary),source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [source,kpad,shard]},scope='Actual virtual motion/intent methods, KPAD filter and generated E1 converter/getter using production PPC helpers. Mocked memory and input IO only. Physical direction acceptance remains separate.')
    if a.arm64:
        report['status']='COMPILED_NOT_EXECUTED'
    else:
        run = subprocess.run([str(binary)],capture_output=True,text=True)
        print(run.stdout.strip())
        report.update(status='EXPECTED_OLD_FAILURE' if a.before else 'PASS',exit_code=run.returncode,output=run.stdout)
        assert run.returncode==(1 if a.before else 0),run.stdout+run.stderr
    (out/(label+'.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__': main()
