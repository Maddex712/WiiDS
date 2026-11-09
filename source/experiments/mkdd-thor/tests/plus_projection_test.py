"""Execute the actual private Plus translations against every course's marker projection.

Usage (Python -I): plus_projection_test.py <private-prepared-plus-folder> <runtime> <cmake>
Generated C/C++ and fixtures are emitted only outside the repository.
"""
from pathlib import Path
import json
import re
import struct
import subprocess
import sys

lab, runtime, cmake = (Path(x).resolve() for x in sys.argv[1:])
repo = Path(__file__).resolve().parents[3]
if lab.is_relative_to(repo):
    raise ValueError('Translated code must stay private')
out = lab / 'projection-test'
out.mkdir(exist_ok=True)
source = (lab / 'generated/recomp_089.c').read_text()
source = source[source.index('static void body_803E2300'):]
other = (lab / 'generated/recomp_041.c').read_text()
other = other[other.index('void fn_8014E610(CPU* c) {'):]
other = other[:other.index('\n}\n') + 3]
defined = set(re.findall(r'void (fn_[0-9A-F]+)\(', source))
called = set(re.findall(r'\b(fn_[0-9A-F]+)\(c\)', source + other))
stubs = '\n'.join(f'void {fn}(CPU*) {{ abort(); }}' for fn in sorted(called - defined - {'fn_80003540'}))
fixtures = json.loads((lab / 'projection-fixtures.json').read_text())
with (out / 'fixtures.bin').open('wb') as f:
    f.write(struct.pack('<I', len(fixtures)))
    for entry in fixtures:
        f.write(struct.pack('<3I4f', entry['key'], entry['orientation'], len(entry['points']), *entry['corners']))
        for point in entry['points']:
            values = []
            for component, name in enumerate(entry['axes']):
                p = point[0 if name[-1] == 'x' else 2] * (-1 if name.startswith('-') else 1)
                values.append((p - entry['bounds'][component]) / (entry['bounds'][component + 2] - entry['bounds'][component]))
            f.write(struct.pack('<5f', *point, *values))
code = r'''#include "recomp_funcs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <climits>
uint8_t* g_mem;
volatile uint64_t g_vcount = 0, g_vlimit = ULLONG_MAX;
uint8_t g_gp_buf[64]; uint32_t g_gp_len;
void gp_flush_line() { abort(); }
uint32_t vm_translate(uint32_t, int) { abort(); }
uint32_t mmio_read32(uint32_t) { abort(); }
uint16_t mmio_read16(uint32_t) { abort(); }
uint8_t mmio_read8(uint32_t) { abort(); }
void mmio_write32(uint32_t, uint32_t) { abort(); }
void mmio_write16(uint32_t, uint16_t) { abort(); }
void mmio_write8(uint32_t, uint8_t) { abort(); }
void irq_poll(CPU*) { abort(); }
void unimpl(CPU*,uint32_t,uint32_t) { abort(); }
void psq_load_quantised(CPU*,uint32_t,int,int,int) { abort(); }
void psq_store_quantised(CPU*,uint32_t,int,int,int) { abort(); }
void fn_80003540(CPU* c) { memcpy(HOST_W(c->r[3]), HOST(c->r[4]), c->r[5]); }
''' + stubs + '\n' + source + '\n' + other + r'''
static unsigned checks;
static void check(bool value, const char* message) {
    ++checks; if (!value) { fprintf(stderr, "FAIL %s, check %u\n", message, checks); exit(1); }
}
static uint32_t word(FILE* f) { uint32_t v; check(fread(&v,4,1,f)==1,"fixture read"); return v; }
static float number(FILE* f) { float v; check(fread(&v,4,1,f)==1,"fixture float"); return v; }
int main(int argc,char** argv) {
    check(argc==3,"arguments");
    g_mem=(uint8_t*)calloc(0x1800000,1); check(g_mem!=nullptr,"memory");
    FILE* dol=fopen(argv[1],"rb"); check(dol!=nullptr,"disc executable");
    uint32_t h[64]; check(fread(h,256,1,dol)==1,"DOL header");
    for(unsigned i=0;i<18;++i) {
        uint32_t size=GCN_BSWAP32(h[0x90/4+i]), addr=GCN_BSWAP32(h[0x48/4+i]), offset=GCN_BSWAP32(h[i]);
        if (!size) continue;
        check((addr&0x3fffffff)+size<=0x1800000,"DOL bounds");
        fseek(dol,offset,SEEK_SET);check(fread(HOST_W(addr),size,1,dol)==1,"DOL section");
    }
    fclose(dol);
    CPU c={}; c.r[1]=0x81700000; c.r[2]=0x803d45a0; c.r[13]=0x803d1420;
    constexpr uint32_t table=0x803e2824;
    for(int direction : {1,-1}) for(int n=0;n<6;++n) {
        unsigned page=(LD8(0x802ed5f9)+direction+5)%5;
        c.r[3]=(uint32_t)direction;fn_803E2300(&c);
        check(LD8(0x802ed5f9)==page,"page switch/wrap");
        check(c.r[1]==0x81700000,"stack preserved");
        for(unsigned i=0;i<74;++i) check(LD8(LD32(table+i*4))==page+'0',"course path changed");
        for(unsigned i=0;i<88;++i) check(LD32(LD32(table+0x128+i*4))==LD32(table+0x2e0+page*352+i*4),"projection floats copied");
        for(unsigned i=0;i<22;++i) check(LD8(LD32(table+0x288+i*4))==LD8(table+0x9c0+page*22+i),"orientation instruction changed");
    }
    FILE* f=fopen(argv[2],"rb"); check(f!=nullptr,"fixtures");
    uint32_t count=word(f);check(count==80,"80 course variants");
    unsigned points=0;
    for(unsigned i=0;i<count;++i) {
        unsigned key=word(f), orientation=word(f), n=word(f);
        float corners[4]; for(auto& v:corners) v=number(f);
        constexpr uint32_t race=0x80010000, pane=0x80020000, parent=0x80030000, point=0x80040000, uv=0x80040100;
        ST32(race+0x34c,pane);ST32(race+0x1b0,parent);
        STF32(pane+0x20,0);STF32(pane+0x24,0);STF32(pane+0x28,1);STF32(pane+0x2c,1);
        STF32(parent+0x50,0);STF32(parent+0x54,0);STF32(parent+0x58,1);
        STF32(race+0x1108,0);STF32(race+0x110c,0);
        for(unsigned k=0;k<4;++k) STF32(race+0x1110+k*4,corners[k]);
        ST32(c.r[13]-23608,0x80050000);ST32(0x80050038,0x80051000);ST8(0x8005102c,0);
        for(unsigned j=0;j<n;++j) {
            for(unsigned k=0;k<3;++k) STF32(point+k*4,number(f));
            float expectedU=number(f),expectedV=number(f);
            c.r[3]=race;c.r[4]=orientation;c.r[5]=point;c.r[6]=uv;
            fn_8014E610(&c);
            float u=LDF32(uv),v=LDF32(uv+4);
            if (fabs(u-expectedU)>0.000002f || fabs(v-expectedV)>0.000002f)
                fprintf(stderr,"key %u point %u: native %.8f,%.8f expected %.8f,%.8f\n",key,j,u,v,expectedU,expectedV);
            check(fabs(u-expectedU)<=0.000002f && fabs(v-expectedV)<=0.000002f,"actual native marker projection matches manifest");
            ++points;
        }
    }
    fclose(f);free(g_mem);
    printf("PASS %u checks: actual Plus page switches both directions/wrap; %u native marker projections across all 80 courses\n",checks,points);
}
'''
code = code.replace('#include <climits>', '#include <climits>\n#include <initializer_list>')
(out / 'projection.cpp').write_text(code, encoding='utf-8')
(out / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(plus_projection LANGUAGES CXX)\n'
    'set(CMAKE_CXX_STANDARD 17)\nadd_executable(plus_projection projection.cpp)\n'
    f'target_include_directories(plus_projection PRIVATE "{runtime.as_posix()}/gcn-recomp/runtime" "{lab.as_posix()}/generated")\n')
subprocess.run([str(cmake), '-S', str(out), '-B', str(out / 'build')], check=True)
subprocess.run([str(cmake), '--build', str(out / 'build'), '--config', 'Release'], check=True)
subprocess.run([str(out / 'build/Release/plus_projection.exe'), str(lab / 'plus.dol'), str(out / 'fixtures.bin')], check=True)
