#include "../native/lower_state.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace wiids::mkdd;
int checks;
void check(bool ok) { ++checks; if (!ok) throw std::runtime_error("check "+std::to_string(checks)); }
std::vector<uint8_t> ram(0x1800000);
constexpr uint32_t mgr=0x80400000, dir=0x80401000, info=0x80402000, course=0x80403000;
constexpr uint32_t ctrl=0x80404000, checker=0x80405000, body=0x80406000, char0=0x80407000, char1=0x80407020;
constexpr uint32_t items=0x80408000, obj=0x80409000, shuffle=0x8040a000;
void w8(uint32_t a, uint8_t v) { ram.at(a-0x80000000)=v; }
void w16(uint32_t a, uint16_t v) { w8(a,v>>8); w8(a+1,v); }
void w32(uint32_t a, uint32_t v) { w16(a,v>>16); w16(a+2,v); }
void wf(uint32_t a, float v) { uint32_t u; std::memcpy(&u,&v,4); w32(a,u); }
float f(int32_t word) { float v; std::memcpy(&v,&word,4); return v; }
void fixture() {
    std::fill(ram.begin(),ram.end(),0);
    w32(0x803cb7e8,mgr); w32(mgr+0x18,dir); w32(mgr+0x38,info);
    w32(dir+0xc,17); w32(dir+0x30,2); w32(info+8,2);
    w16(info+0x1c,8); w16(info+0x1e,1); w16(info+0x20,1); w16(info+0x114,0);
    w16(mgr+0x2e,3); w32(mgr+0x44,course); w32(course,0x24);
    w32(mgr+0x48,checker); w32(checker+0x88,2); w32(checker+0x2c,1);
    w32(checker+0x84,12345); wf(checker+0x48,0.5f);
    w32(info+0x38,char0); w32(info+0x40,char1); w16(char0,7); w16(char1,8);
    w32(0x803cc588,ctrl); w32(ctrl+0xa0,body); wf(body+0x118,1);
    wf(body+0x11c,-300); wf(body+0x12c,20); wf(body+0x13c,900); wf(body+0x444,10);
}
int main() {
    Memory m(ram.data(),ram.size());
    check(!m.span(0xffffffff,16)); check(!m.span(0x7ffffffc,4));
    check(!m.span(0x81800000,1)); check(m.span(0x817ffffc,4));
    check(!m.span(0x817ffffc,8)); check(!m.span(0x80000001,4));
    check(m.span(0x80000001,1,1)); check(!m.span(0x80000000,SIZE_MAX));
    check(!sample(m,1)[Flags]);
    fixture(); auto p=sample(m,42); size_t b=kHeader;
    check(p[Magic]==kMagic && p[Words]==144 && p[Frame]==42);
    check(p[Flags]==Race && p[Course]==0x24 && p[KartCount]==8);
    check(p[b+Rank]==2 && p[b+Lap]==1 && p[b+TimeMs]==12345);
    check(p[b+Character0]==7 && p[b+Character1]==8 && p[b+Driver]==0);
    check(f(p[b+X])==-300 && f(p[b+Z])==900);
    check(std::abs(f(p[b+Heading])-1.5707963f)<0.001f);
    check(std::abs(f(p[b+SpeedKmh])-21.6f)<0.001f);
    w32(body+0x578,0x200); check(f(sample(m,42)[b+SpeedKmh])==0); w32(body+0x578,0);
    check(p[b+Item0]==-1 && !(p[b+Valid]&Items));
    check(p[b+kStride+Rank]==-1 && p[b+kStride+Valid]==0);
    w8(0x803cb6dc,1); check(sample(m,43)[Flags]&Paused);
    w32(info+0x28,1); check(sample(m,44)[Flags]&Demo);
    w32(info+0x1c,0xffff0001); check(!sample(m,45)[Flags]); fixture();
    w32(mgr+0x18,0xfffffffc); check(!sample(m,46)[Flags]); fixture();
    w32(mgr+0x48,0x817ffffc); check(sample(m,47)[b+Rank]==-1); fixture();
    wf(body+0x11c,std::numeric_limits<float>::quiet_NaN());
    check(!(sample(m,48)[b+Valid]&Position)); fixture();
    wf(body+0x444,std::numeric_limits<float>::infinity());
    check(!(sample(m,49)[b+Valid]&Position)); fixture();
    w8(body+0x5b2,2); check(sample(m,50)[b+Driver]==-1); fixture();
    w32(checker+0x84,5999999); check(sample(m,51)[b+TimeMs]==-1); fixture();
    w16(char0,21); check(sample(m,52)[b+Character0]==-1); fixture();
    w32(checker+0x88,9); check(sample(m,53)[b+Rank]==-1); fixture();
    w32(info+8,4); p=sample(m,54); check(p[TotalLaps]==0 && p[b+Lap]==-1); fixture();
    auto before=ram; p=sample(m,55); check(ram==before);
    w32(0x803cbf40,items); w32(items+0x24c,16); w32(items+0x254,16);
    w32(items+0x35c,shuffle); w32(items+0x360,shuffle+0x10);
    p=sample(m,56); check(p[b+Item0]==16 && p[b+Item1]==16 && (p[b+Valid]&Items));
    w32(items+0x2cc,obj); w32(obj+0x7c,2); w32(obj+0x120,0); w8(obj+0x124,0);
    p=sample(m,57); check(p[b+Item0]==2 && p[b+Item1]==16);
    w8(obj+0x124,1); check(!(sample(m,58)[b+Valid]&Items)); w8(obj+0x124,0);
    w32(obj+0x120,1); check(!(sample(m,59)[b+Valid]&Items)); w32(obj+0x120,0);
    w32(obj+0x7c,999); check(!(sample(m,60)[b+Valid]&Items)); w32(obj+0x7c,2);
    w32(items+0x254,3); check(!(sample(m,61)[b+Valid]&Items));
    w32(items+0x254,16); w8(shuffle+0x16,1); check(!(sample(m,62)[b+Valid]&Items));
    w8(shuffle+0x16,2); check(!(sample(m,63)[b+Valid]&Items));
    w8(shuffle+0x16,0); check(sample(m,64)[b+Valid]&Items);
    fixture(); p=sample(m,62);
    publish(p); p[b+Rank]=99; check(latest()[b+Rank]==2);
    std::thread producer([] { for (int i=0;i<10000;i++) { auto q=unavailable(i); q[Course]=i; publish(q); } });
    bool coherent=true;
    for(int i=0;i<10000;i++) { auto q=latest(); if(q[Flags]==0 && q[Course]!=q[Frame]) coherent=false; }
    producer.join(); check(coherent);
    publish(unavailable()); check(latest()[Flags]==0);
    std::printf("PASS %d native lower-state checks; synthetic RAM, no game-play evidence\n",checks);
}
