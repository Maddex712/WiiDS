#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <vector>
using u32=uint32_t;using u16=uint16_t;using u64=uint64_t;
using AuroraReadbackStream=uint64_t;
constexpr bool GX_FALSE=false;constexpr int GX_READ_FF=0,AURORA_READBACK_PEEK_ARGB=0;
struct AuroraReadbackDest{u64 key;void* dest;size_t size;bool found;};
struct AuroraReadbackPeekRequest{u64 key;int kind;u16 x,y;};
namespace aurora {
struct ByteReader{ByteReader(const uint8_t*,size_t){}};
namespace gfx {
uint64_t frame=0;uint64_t current_frame(){return frame;}
namespace readback {
struct Output{u64 key;uint32_t payloadOffset,payloadSize;};
struct Packet{
 u64 sequence=0;std::vector<Output> outputs;std::vector<uint8_t> payload;
 const Output* find(u64 key)const{for(auto& o:outputs)if(o.key==key)return &o;return nullptr;}
 std::span<const uint8_t> payload_of(const Output& o)const{return {payload.data()+o.payloadOffset,o.payloadSize};}
};
std::shared_ptr<const Packet> packet,replaceAfterSnapshot;
bool available=true;bool supported(){return available;}
std::shared_ptr<const Packet> latest(u64){auto saved=packet;if(replaceAfterSnapshot){packet=replaceAfterSnapshot;replaceAfterSnapshot.reset();}return saved;}
u64 requested=0;u64 next_sequence(u64){return ++requested;}
}}
namespace gx::readback{int captures=0;void handle_capture_peeks(ByteReader&){++captures;}}
}
AuroraReadbackStream AuroraReadbackCreateStream(){return 1;}
bool AuroraReadbackGet(u64,AuroraReadbackDest* dests,u32 count){
 auto packet=aurora::gfx::readback::available?aurora::gfx::readback::packet:nullptr;bool any=false;
 for(u32 i=0;i<count;i++){auto& d=dests[i];d.found=false;if(!packet)continue;
  auto* o=packet->find(d.key);if(!o||o->payloadSize!=d.size)continue;
  std::memcpy(d.dest,packet->payload.data()+o->payloadOffset,d.size);d.found=true;any=true;}
 return any;
}
void WiidsXD_PeekDisplay(u64){}
namespace {
std::atomic_bool titleProbeEnabled{false},fieldProbeEnabled{false};
std::atomic_int fieldColor{0xA0A0A0},topLight{1000},skyClean{1};
}
#include "probe_body.inc"
static int checks;
void check(bool b,const char* message){++checks;if(!b){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
void tick(uint64_t frame){aurora::gfx::frame=frame;WiidsXD_TopProbe();}
auto image(u64 sequence,int gray,bool panel=false,bool grid=true,bool sky=true){
 auto p=std::make_shared<aurora::gfx::readback::Packet>();p->sequence=sequence;
 for(int i=0;i<27;i++){
  if((i<24&&!grid)||(i>=24&&!sky))continue;
  u32 color=i<24?0xff000000u|gray*0x10101u:(panel?0xff808080u:0xff3296dcu);
  p->outputs.push_back({(u64)i,(uint32_t)p->payload.size(),4});
  const auto* bytes=(const uint8_t*)&color;p->payload.insert(p->payload.end(),bytes,bytes+4);
 }return p;
}
int main(int argc,char** argv){
 check(argc==2,"scenario required");const char* scenario=argv[1];
 using namespace aurora::gfx::readback;
 if(!std::strcmp(scenario,"cadence")){
  fieldProbeEnabled=true;tick(100);check(aurora::gx::readback::captures==1,"field capture");
  fieldProbeEnabled=false;titleProbeEnabled=true;tick(101);check(aurora::gx::readback::captures==2,"title must not inherit field deadline");
  tick(102);check(aurora::gx::readback::captures==2,"title retains two-frame cadence");
  tick(103);check(aurora::gx::readback::captures==3,"next title sample");
 }else if(!std::strcmp(scenario,"inactive")){
  fieldProbeEnabled=true;tick(100);fieldProbeEnabled=false;tick(101);fieldProbeEnabled=true;tick(102);
  check(aurora::gx::readback::captures==2,"reentering active scene expires old deadline");
 }else if(!std::strcmp(scenario,"sky_stale")){
  titleProbeEnabled=true;tick(0);packet=image(1,255,true);
  for(int frame=2;frame<=24;frame+=2)tick(frame);
  check(skyClean==1,"one completed packet cannot satisfy six-capture debounce");
  for(int seq=2;seq<=6;seq++){packet=image(seq,255,true);tick(24+seq*2);}
  check(skyClean==0,"six distinct panel captures change classification");
 }else if(!std::strcmp(scenario,"brightness_stale")){
  titleProbeEnabled=true;tick(0);packet=image(1,255);tick(2);packet=image(2,180);tick(4);
  int dim=topLight;check(dim>0&&dim<900,"genuine gradual dim recorded");
  for(int frame=6;frame<=60;frame+=2)tick(frame);
  check(topLight==dim,"stale packet cannot advance settling and relight lower screen");
  for(int seq=3;seq<=22;seq++){packet=image(seq,180);tick(60+seq*2);}
  check(topLight==1000,"twenty genuinely fresh steady readings can settle reference");
 }else if(!std::strcmp(scenario,"fresh")){
  titleProbeEnabled=true;tick(0);packet=image(1,255);tick(2);check(topLight==1000,"bright frame");
  packet=image(2,0);tick(4);check(topLight==0,"actual black remains black");
  packet=image(3,255);tick(6);check(topLight==1000,"actual bright recovery");
  for(int seq=4;seq<=9;seq++){packet=image(seq,255,true);tick(seq*2);}
  check(skyClean==0,"fresh panel debounce");
  for(int seq=10;seq<=15;seq++){packet=image(seq,255,false);tick(seq*2);}
  check(skyClean==1,"fresh clean-sky debounce");
 }else if(!std::strcmp(scenario,"partial")){
  titleProbeEnabled=true;tick(0);packet=image(1,255);tick(2);
  packet=image(2,0,true,false,true);tick(4);
  check(topLight==1000,"missing grid is not a black frame (defensive partial-packet check)");
 }else if(!std::strcmp(scenario,"old_mode")){
  fieldProbeEnabled=true;tick(100);packet=image(1,0);fieldProbeEnabled=false;titleProbeEnabled=true;tick(160);
  check(topLight==1000,"new mode does not use older-mode packet");
  packet=image(2,255);tick(162);check(topLight==1000,"new mode's completed capture accepted");
 }else if(!std::strcmp(scenario,"mode_fade")){
  titleProbeEnabled=true;tick(0);packet=image(1,255);tick(2);
  titleProbeEnabled=false;fieldProbeEnabled=true;tick(4);packet=image(3,200);tick(64);
  check(topLight>800&&topLight<950,"mode transition must retain fade reference instead of relighting a fading frame");
 }else if(!std::strcmp(scenario,"unsupported")){
  titleProbeEnabled=true;tick(0);packet=image(1,255);tick(2);
  packet=image(2,180);available=false;tick(4);check(topLight==1000,"disabled readback retains previous light");
  available=true;tick(6);check(topLight>0&&topLight<900,"reenabled readback consumes previously undelivered packet");
 }else if(!std::strcmp(scenario,"snapshot")){
  fieldProbeEnabled=true;tick(0);packet=image(1,80);replaceAfterSnapshot=image(2,200);tick(60);
  check(fieldColor==0x505050,"sequence and pixels must come from the same immutable packet");
 }else{check(false,"unknown scenario");}
 std::printf("PASS %s %d checks\n",scenario,checks);
}
