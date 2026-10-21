#include "thor_reserve.h"
#include "abi_bridge.h"
#include "memory.h"
#include "netplay_session.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

unsigned checks=0, calls=0;
bool failSpawn=false;
constexpr uint32_t player=0x81000000,item=0x81100000,spawned=0x81200000,manager=0x81300000;
uint32_t setting=0,posY=0;
void Check(bool b,const char* msg) { ++checks; if(!b) { std::fprintf(stderr,"FAIL %s\n",msg); std::exit(1); } }
void InvokeIndirectCpu(uint32_t target, CpuContext* c) {
 if(target==0x8005F900) { c->gpr[3]=player; return; }
 Check(target==0x80064610,"only original actor constructor");
 Check(c->gpr[3]==60,"original EN_ITEM profile");
 Check(c->gpr[6]==0 && c->gpr[7]==0,"original null rotation and player layer");
 setting=c->gpr[4]; Memory::TryRead32(c->gpr[5]+4,posY); ++calls;
 c->gpr[3]=failSpawn?0:spawned;
}
void Setup(CpuContext& c) {
 c.gpr[1]=0x817f0000;c.gpr[13]=0x80430000;c.lr=0x80a286a4;c.gpr[31]=item;c.gpr[29]=2;
 Memory::TryWrite32(0x80428730,5);Memory::TryWrite32(0x80315b98,0);
 Memory::TryWrite32(0x80354e50,1);Memory::TryWrite32(c.gpr[13]-22080,manager);
 Memory::Half(player+8,13);Memory::Half(item+8,60);Memory::Byte(item+0xdff,0);
 Memory::TryWrite32(player+0x1090,2);Memory::TryWrite32(player+0x14e0,2);
 Memory::TryWrite32(player+0xac,std::bit_cast<uint32_t>(100.0f));
 Memory::TryWrite32(player+0xb0,std::bit_cast<uint32_t>(200.0f));
 Memory::TryWrite32(player+0xb4,0);Memory::TryWrite32(item,1);Memory::TryWrite32(spawned,90);
 Memory::Half(item+0xdca,1);
}
int main(int argc,char** argv) {
 setenv("NSMBW_THOR","1",1);
 const auto root=std::filesystem::path(std::getenv("THOR_RESERVE_TEST_ROOT"));std::filesystem::create_directories(root/"NAND");
 CpuContext c;Setup(c);
 if(argc>1 && std::string(argv[1])=="sibling") {
  setenv("THOR_RESERVE_TEST_NAND_NAME","NAND-alt",1);
  std::filesystem::create_directories(root/"NAND-alt");
  ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==-1,"same-parent configured NAND has separate reserve");
  ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);
  Check(ThorReserve::ReadWords()[2]==1,"sibling profile independently stores fire");
  std::printf("PASS sibling roots %u\n",checks);return 0;
 }
 if(argc>1 && std::string(argv[1])=="corrupt") {
  // Select the reserve produced by save0 ice above, independently of its path
  // hash algorithm, so the test exercises real profile resolution.
  std::filesystem::path sidecar;
  for(const auto& entry:std::filesystem::directory_iterator(root)) {
   if(entry.path().extension()!=".bin")continue;
   std::ifstream in(entry.path(),std::ios::binary);unsigned char bytes[20]{};in.read(reinterpret_cast<char*>(bytes),20);
   if(in.gcount()==20 && bytes[8]==4)sidecar=entry.path();
  }
  Check(!sidecar.empty(),"resolved primary profile sidecar found");
  {std::ofstream out(sidecar,std::ios::binary|std::ios::trunc);out<<"bad";}
  ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==-1,"corrupt file cannot create item");
  Check(std::filesystem::exists(sidecar.string()+".invalid-0"),"corrupt sidecar preserved");
  std::ifstream in(sidecar.string()+".invalid-0");std::string value;in>>value;Check(value=="bad","corrupt bytes unchanged");
  std::printf("PASS corruption %u\n",checks);return 0;
 }
 if(argc>1 && std::string(argv[1])=="reload") {
  ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==4,"sidecar reload retains save0 ice");
  Memory::Byte(manager+38,1);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==-1,"save1 isolated on reload");
  std::printf("PASS reload %u\n",checks);return 0;
 }
 ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==-1,"starts empty");Check(!ThorReserve::RequestUse(),"empty rejects");
 c.lr=0;ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==-1,"wrong callsite rejects");
 c.lr=0x80a286a4;ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==1,"duplicate fire stored");
 ThorReserve::ObserveAcceptedPickup(&c);Check(ThorReserve::ReadWords()[7]==1,"repeated item cannot duplicate");
 Memory::TryWrite32(0x80429f48,1);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==1,"pause retains visible slot");Check(!ThorReserve::RequestUse(),"pause rejects use");
 Memory::TryWrite32(0x80429f48,0);ThorReserve::Tick(&c);Check(ThorReserve::RequestUse(),"use queued");Check(!ThorReserve::RequestUse(),"held request coalesces");
 failSpawn=true;ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==1,"spawn failure retains reserve");Check(setting==0x08000009,"fire maps9 with original FreeDrop create type8");Check(std::bit_cast<float>(posY)==264.0f,"spawn above real player");
 failSpawn=false;Check(ThorReserve::RequestUse(),"retry queues");ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==-1,"successful spawn consumes exactly once");
 Memory::TryWrite32(item,90);ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==1,"caught spare of the same form returns to the box (NSMB2)");
 ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[7]==2,"repeated notification stores once");
 Memory::TryWrite32(item,2);Memory::Half(item+0xdca,4);ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==4,"new ice pickup replaces reserve");
 Memory::Byte(manager+38,1);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==-1,"save selection isolates reserve");
 Memory::Byte(manager+38,0);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==4,"return restores save0 slot");
 NetplaySession::active=true;ThorReserve::Tick(&c);
 Check(!ThorReserve::RequestUse(),"one-player LAN session rejects host-side reserve request");
 Memory::TryWrite32(item,7);Memory::Half(item+0xdca,1);ThorReserve::ObserveAcceptedPickup(&c);
 NetplaySession::active=false;ThorReserve::Tick(&c);
 Check(ThorReserve::ReadWords()[2]==4,"LAN pickups cannot modify offline reserve");
 Memory::TryWrite32(0x80354e54,1);Memory::TryWrite32(item,3);ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[3]==0,"multiplayer rejects use");
 Memory::TryWrite32(0x80354e54,0);Memory::TryWrite32(0x80315b98,2);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[3]==0,"title demo rejects use");
 Memory::TryWrite32(0x80315b98,0);Memory::TryWrite32(player+0x1014,16);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[3]==0,"dead player rejects use");
 Memory::TryWrite32(player+0x1014,0);Memory::Byte(player+0x1460,1);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[3]==0,"demo player rejects use");
 Memory::Byte(player+0x1460,0);Memory::TryWrite32(player+0x14e0,6);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[3]==0,"transformation rejects use");
 Memory::TryWrite32(player+0x14e0,2);ThorReserve::Tick(&c);Check(ThorReserve::RequestUse(),"last request queues");
 Memory::Byte(manager+38,1);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==-1,"queued save0 request cannot consume save1");
 Memory::Byte(manager+38,0);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==4,"save0 queue cancellation retains item");
 // NSMB2 storage priority.
 Memory::TryWrite32(item,20);Memory::Half(item+0xdca,0);c.gpr[29]=2;ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==4,"mushroom never replaces a better stored item");
 Memory::TryWrite32(player+0x1090,1);Memory::TryWrite32(player+0x14e0,1);Memory::TryWrite32(item,21);Memory::Half(item+0xdca,1);c.gpr[29]=1;ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==4,"Super Mario's mushroom is not stored on powering up");
 Memory::TryWrite32(player+0x1090,2);Memory::TryWrite32(player+0x14e0,2);Memory::TryWrite32(item,22);Memory::Half(item+0xdca,11);ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==1,"changing power-ups stores the displaced fire flower");
 Memory::TryWrite32(player+0x1090,3);Memory::TryWrite32(player+0x14e0,3);Memory::TryWrite32(item,23);Memory::Half(item+0xdca,0);ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==1,"Mini Mario turning Super keeps nothing");
 Memory::TryWrite32(item,24);Memory::Half(item+0xdca,13);c.gpr[29]=2;ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==13,"duplicate mini mushroom replaces fire");
 Memory::TryWrite32(item,25);Memory::Half(item+0xdca,0);ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==0,"mushroom replaces a stored mini mushroom");
 // Same-form use and the guided drop from the top of the screen.
 Memory::TryWrite32(player+0x1090,2);Memory::TryWrite32(player+0x14e0,2);Memory::TryWrite32(item,26);Memory::Half(item+0xdca,1);ThorReserve::ObserveAcceptedPickup(&c);
 constexpr uint32_t bg=0x81400000;Memory::TryWrite32(c.gpr[13]-22656,bg);
 Memory::TryWrite32(bg+0x3c,std::bit_cast<uint32_t>(400.0f));Memory::TryWrite32(bg+0x44,std::bit_cast<uint32_t>(228.0f));
 Memory::Half(spawned+8,60);
 ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==1&&ThorReserve::ReadWords()[3]==1,"fire stored and usable while Mario is Fire");
 Check(ThorReserve::RequestUse(),"same power-up can be dropped (NSMB2)");ThorReserve::Tick(&c);
 Check(ThorReserve::ReadWords()[2]==-1,"drop consumes the box");Check(std::bit_cast<float>(posY)==372.0f,"item appears at the top of the screen");
 auto F=[](uint32_t a){uint32_t v=0;Memory::TryRead32(a,v);return std::bit_cast<float>(v);};
 for(int i=0;i<20;++i)ThorReserve::Tick(&c);
 Check(F(spawned+0xac)==100.0f&&F(spawned+0xb0)==372.0f,"hovers at Mario's x along the screen top");
 Memory::TryWrite32(player+0xac,std::bit_cast<uint32_t>(150.0f));
 for(int i=0;i<25;++i)ThorReserve::Tick(&c);
 Check(F(spawned+0xac)==150.0f,"follows Mario sideways while hovering");
 Memory::TryWrite32(player+0xac,std::bit_cast<uint32_t>(10.0f));
 ThorReserve::Tick(&c);Check(F(spawned+0xac)==150.0f&&F(spawned+0xb0)<372.0f,"then stops following and falls");
 int frames=0;float last=F(spawned+0xb0);
 while(frames<400){ThorReserve::Tick(&c);++frames;float y=F(spawned+0xb0);if(y==last)break;last=y;}
 Check(last<=224.0f&&last>216.0f,"falls through to Mario's head height then hands off");
 Memory::TryWrite32(spawned+0xb0,std::bit_cast<uint32_t>(50.0f));ThorReserve::Tick(&c);Check(F(spawned+0xb0)==50.0f,"original FreeDrop owns the item after hand-off");
 Memory::TryWrite32(item,27);Memory::Half(item+0xdca,4);ThorReserve::ObserveAcceptedPickup(&c);ThorReserve::Tick(&c);Check(ThorReserve::ReadWords()[2]==4,"ice stored for the reload stage");
 std::printf("PASS reserve actual source %u checks, %u original actor calls\n",checks,calls);
}
