// Private host verification only. File commands enter the same scoped transport
// as JNI, or the port's original virtual-controller script. No OS-wide input.
#include "xd_companion.h"
#include "xd_chain.h"
#include <aurora/aurora.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
extern "C" {
uint32_t VIGetRetraceCount();uint32_t PortVI_GetFlipCount();
bool PortInput_LoadScript(const char*);
void PortXD_ScreenshotTo(void*,const char*,uint32_t,uint32_t);
int WiidsXD_MenuOpen(int);int WiidsXD_RuntimeSet(int,int);int WiidsXD_RuntimeRead(int*,int);
void WiidsXD_SetPauseProbe(void(*)());
uint32_t WiidsXD_LogicTicks();float timeGetLastFrameSec();
void WiidsXD_HostDev(const char*);
void PortInput_SetScriptStick(bool(*)(uint32_t,int8_t*,int8_t*));
void heroMoveGetHeroPos(void*);
int fightFloor_GetFightKind(void*);uint16_t fightFloor_GetEncountDataId(void*);uint16_t fightGetFightEncountDataId();
uintptr_t fightFloorGetStatus(void*,uint16_t,uint16_t,uint16_t);uint32_t deckGetTrainerSize();
void* fightKindDataBiosGetPtr(uint16_t);uint8_t fightKindDataBiosGetNigeruFlag(void*);
bool itemDataCheckValid(uint16_t);uint8_t* itemDataBiosGetPtr(uint16_t);uint32_t itemDataBiosGetName(uint8_t*);uint8_t itemDataBiosGetKind(uint8_t*);
const uint16_t* GSmsgGetGSchar(uint32_t);
}
class DeckTrainer{public:static DeckTrainer* getDeckTrainerObj(uint32_t);uint16_t getDarkPokemonDataID(uint8_t) const;};
static uint32_t stickStart=0,stickEnd=0;static int8_t stickX=0,stickY=0;
static bool stickProbe(uint32_t field,int8_t* x,int8_t* y){
    if(field<stickStart||field>=stickEnd)return false;*x=stickX;*y=stickY;return true;
}
static void text(FILE* f,const uint16_t* s,int max=48){
    std::fputc('"',f);
    for(int i=0;i<max&&s[i];i++){
        unsigned c=s[i];
        if(c=='"'||c=='\\')std::fprintf(f,"\\%c",c);
        else if(c>=32&&c<127)std::fputc(c,f);
        else std::fprintf(f,"\\u%04x",c);
    }
    std::fputc('"',f);
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "xd_spsc.h"
#include <thread>
#include <atomic>
#include <array>
struct ProbeSample {
    uint64_t now=0,cycles=0;uint32_t field=0,picture=0,logic=0;float dt=0;
    XdSnapshot snapshot{};int runtime[12]={},battleDebug[4]={};
    float position[3]={};bool positionValid=false;uint64_t drops=0,phases[24]={};
};
static uint64_t phaseTimes[24]={};
extern "C" void WiidsXD_HostPhase(int phase){if(phase>=0&&phase<24)phaseTimes[phase]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct ProbeCommand {char text[256]={};};
struct ProbeEvent {char text[256]={};};
static const char* probeRoot=std::getenv("XD_WIIDS_BRIDGE");
struct ProbeIO {
    XdSpsc<ProbeSample,128> samples;XdSpsc<ProbeCommand,64> commands;
    XdSpsc<ProbeEvent,512> events;std::atomic<uint64_t> droppedEvents{0};
    std::atomic<uint64_t> dropped{0};
    ProbeIO(){std::thread([this]{run();}).detach();}
    void run(){
        const char* root=probeRoot;std::filesystem::create_directories(root);
        FILE* log=std::fopen((std::string(root)+"/state.jsonl").c_str(),"a");
        // Owner stopped all frame-rate and frame-time measurements for pass4.
        FILE *timing=nullptr,*detail=nullptr,*phases=nullptr,*motion=nullptr;
        FILE* eventLog=std::fopen((std::string(root)+"/events.log").c_str(),"a");
        if(!log)return;
        uint64_t previous=0,previousCycles=0,lastState=0,lastFlush=0,lastRead=0,epoch=0;
        int previousRuntime[12]={};uint32_t available=2;std::streamoff consumed=0;
        for(;;){
            ProbeSample sample{};bool worked=false;
            ProbeEvent event{};while(events.pop(event)){worked=true;if(eventLog)std::fprintf(eventLog,"%s\n",event.text);}
            while(samples.pop(sample)){
                worked=true;auto& s=sample.snapshot;auto* runtime=sample.runtime;
                auto field=sample.field,picture=sample.picture;
                if(timing&&previous){
                    std::fprintf(timing,"%u,%u,%llu,%u,%.9g\n",field,picture,(unsigned long long)(sample.now-previous),sample.logic,sample.dt);
                    std::fprintf(detail,"%u,%u,%llu,%llu,%llu\n",field,picture,(unsigned long long)(sample.now-previous),(unsigned long long)(sample.cycles-previousCycles),(unsigned long long)sample.drops);
                    std::fprintf(phases,"%u,%u,%llu",field,picture,(unsigned long long)sample.now);
                    for(auto stamp:sample.phases)std::fprintf(phases,",%llu",(unsigned long long)stamp);std::fputc('\n',phases);
                }
                previous=sample.now;previousCycles=sample.cycles;
                if(motion&&sample.positionValid)std::fprintf(motion,"%u,%u,%u,%.9g,%.9g,%.9g,%.9g\n",field,picture,sample.logic,sample.dt,sample.position[0],sample.position[1],sample.position[2]);
                if(s.epoch!=epoch||s.available!=available||std::memcmp(runtime,previousRuntime,sizeof(previousRuntime))||sample.now-lastState>=100000000){
                    epoch=s.epoch;available=s.available;lastState=sample.now;std::memcpy(previousRuntime,runtime,sizeof(previousRuntime));
        std::fprintf(log,"{\"field\":%u,\"picture\":%u,\"epoch\":%llu,\"available\":%u,\"menu\":%u,\"phase\":%u,\"battle\":%u,\"actor\":%u,\"target\":%u,\"map\":%u,\"progress\":%u,\"area\":",field,picture,(unsigned long long)s.epoch,s.available,s.menu_id,s.phase,s.battle,s.actor,s.target,s.map_id,s.story_progress);
        text(log,s.map_name);std::fprintf(log,",\"objective\":");text(log,s.objective,96);
        std::fprintf(log,",\"choices\":[");
        for(uint32_t i=0;i<s.choice_count;i++){auto& c=s.choices[i];if(i)std::fputc(',',log);
            std::fprintf(log,"{\"action\":%d,\"arg\":%d,\"item\":%d,\"enabled\":%d,\"label\":",c.action,c.argument,c.item_id,c.enabled);text(log,c.label);std::fputc('}',log);}
        std::fprintf(log,"],\"runtime\":[");for(int i=0;i<12;i++)std::fprintf(log,"%s%d",i?",":"",runtime[i]);
        std::fprintf(log,"],\"battle_debug\":[%d,%u,%u,%u",sample.battleDebug[0],sample.battleDebug[1],sample.battleDebug[2],sample.battleDebug[3]);
        std::fprintf(log,"],\"acting\":{\"species\":%d,\"hp\":%d,\"max_hp\":%d,\"slot\":%d,\"name\":",s.acting.species,s.acting.hp,s.acting.max_hp,s.actor_party_slot);text(log,s.acting.name);
        std::fprintf(log,"},\"targeted\":{\"species\":%d,\"hp\":%d,\"max_hp\":%d,\"index\":%d,\"name\":",s.targeted.species,s.targeted.hp,s.targeted.max_hp,s.target_index);text(log,s.targeted.name);
        std::fprintf(log,"},\"moves\":[");
        for(int i=0;i<4;i++){auto& m=s.moves[i];if(i)std::fputc(',',log);std::fprintf(log,"{\"id\":%d,\"pp\":%d,\"max_pp\":%d,\"type\":%d,\"legal\":%d,\"num\":%d,\"den\":%d,\"info\":%d,\"name\":",m.id,m.pp,m.max_pp,m.type,m.legal,m.effectiveness_num,m.effectiveness_den,m.effectiveness_info);text(log,m.name);std::fprintf(log,",\"type_name\":");text(log,s.move_type_names[i]);std::fputc('}',log);}
        std::fprintf(log,"],\"party\":[");
        for(int i=0;i<6;i++){if(i)std::fputc(',',log);auto& p=s.party[i];std::fprintf(log,"{\"species\":%d,\"hp\":%d,\"status\":%d,\"shadow\":%d,\"heart\":%d,\"heart_max\":%d,\"name\":",p.species,p.hp,p.status,p.shadow_id,p.heart,p.heart_max);text(log,p.name);std::fputc('}',log);}
        std::fprintf(log,"],\"shadows\":[");
        for(uint32_t i=0;i<s.shadow_count;i++){auto& d=s.shadows[i];std::fprintf(log,"%s{\"id\":%d,\"species\":%d,\"snagged\":%d,\"missed\":%d,\"heart\":%d}",i?",":"",d.id,d.species,d.snagged,d.missed,d.heart);}
        std::fprintf(log,"]}\n");

                }
            }
            uint64_t now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if(now-lastRead>=2000000){
                lastRead=now;std::ifstream file(std::string(root)+"/commands.txt",std::ios::binary);
                if(file){file.seekg(consumed);std::string line;
                    while(std::getline(file,line)){
                        if(file.eof())break; // incomplete append: leave offset until newline arrives
                        if(line.size()>=sizeof(ProbeCommand::text))break;
                        ProbeCommand command{};std::memcpy(command.text,line.data(),line.size());
                        if(!commands.push(command))break;
                        consumed+=static_cast<std::streamoff>(line.size()+1);
                    }
                }
            }
            if(now-lastFlush>=20000000){lastFlush=now;std::fflush(log);if(eventLog)std::fflush(eventLog);}
            if(!worked)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};
// Process-lifetime queues keep worker ownership independent of CRT teardown.
static ProbeIO& probeIO(){static ProbeIO* value=new ProbeIO;return *value;}
static void event(const char* format,unsigned long long a,int b,int c,int d=0){
    ProbeEvent out{};std::snprintf(out.text,sizeof(out.text),format,a,b,c,d);
    auto& io=probeIO();if(!io.events.push(out))io.droppedEvents++;
}
static void chainTrace(const char* text,int kind,int menu,int state){
    ProbeEvent out{};std::snprintf(out.text,sizeof(out.text),"[XD chain] %s kind %d menu %x state %d",text,kind,menu,state);
    auto& io=probeIO();if(!io.events.push(out))io.droppedEvents++;
}
extern "C" void WiidsXD_HostFrame(){
    const char* root=probeRoot;if(!root||!*root)return;
    auto& io=probeIO();WiidsXD_SetPauseProbe(WiidsXD_HostFrame);
    static bool initialized=false;if(!initialized){initialized=true;PortInput_SetScriptStick(stickProbe);WiidsXD_SetChainTrace(chainTrace);}
    ProbeSample sample{};
    sample.now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    sample.field=VIGetRetraceCount();sample.picture=PortVI_GetFlipCount();
    WiidsXD_Read(&sample.snapshot);WiidsXD_RuntimeRead(sample.runtime,12);
    auto& s=sample.snapshot;auto field=sample.field,picture=sample.picture;
    if(s.battle){sample.battleDebug[0]=fightFloor_GetFightKind(nullptr);sample.battleDebug[1]=fightGetFightEncountDataId();sample.battleDebug[2]=fightFloor_GetEncountDataId(nullptr);sample.battleDebug[3]=(int)fightFloorGetStatus(nullptr,0,0x22,0);}
    sample.positionValid=false;
    sample.drops=io.dropped.load();if(!io.samples.push(sample))io.dropped++;
    for(int count=0;count<16;count++){
        ProbeCommand command{};if(!io.commands.pop(command))break;
        std::string line(command.text);if(!line.empty()&&line.back()=='\r')line.pop_back();
        unsigned long long epoch;int action,arg;
        if(std::sscanf(line.c_str(),"touch %llu %d %d",&epoch,&action,&arg)==3){
            int accepted=WiidsXD_Submit(epoch,action,arg);event("[XD host touch] epoch %llu action %d arg %d accepted %d",epoch,action,arg,accepted);
        }else if(std::sscanf(line.c_str(),"menu %d",&action)==1){
            event("[XD host menu] field %llu open %d accepted %d",field,action,WiidsXD_MenuOpen(action));
        }else if(std::sscanf(line.c_str(),"setting %d %d",&action,&arg)==2){
            event("[XD host setting] field %llu key %d value %d accepted %d",field,action,arg,WiidsXD_RuntimeSet(action,arg));
        }else if(line=="inspect kinds"){
            for(uint16_t i=0;i<32;i++)if(auto* kind=fightKindDataBiosGetPtr(i))std::fprintf(stderr,"[XD kind] %u run %u\n",i,fightKindDataBiosGetNigeruFlag(kind));
        }else if(line=="inspect items"){
            for(uint16_t i=1;i<64;i++)if(itemDataCheckValid(i)){auto* item=itemDataBiosGetPtr(i);std::fprintf(stderr,"[XD item] %u kind %u name ",i,itemDataBiosGetKind(item));text(stderr,GSmsgGetGSchar(itemDataBiosGetName(item)));std::fputc('\n',stderr);}
        }else if(line=="inspect trainers"){
            uint32_t count=deckGetTrainerSize();if(count>8192)continue;
            for(uint32_t i=1;i<count;i++)if(auto* trainer=DeckTrainer::getDeckTrainerObj(i))for(uint8_t slot=0;slot<6;slot++)if(auto dark=trainer->getDarkPokemonDataID(slot))std::fprintf(stderr,"[XD trainer] trainer %u slot %u shadow %u\n",i,slot,dark);
        }else if(line.rfind("stick ",0)==0){int x,y,n;
            if(std::sscanf(line.c_str(),"stick %d %d %d",&x,&y,&n)==3&&x>=-128&&x<=127&&y>=-128&&y<=127&&n>0&&n<=600){stickX=(int8_t)x;stickY=(int8_t)y;stickStart=field+2;stickEnd=stickStart+n;}
        }else if(line.rfind("dev ",0)==0){WiidsXD_HostDev(line.c_str()+4);
        }else if(line.rfind("pad ",0)==0){char button[64];unsigned held=2;
            if(std::sscanf(line.c_str(),"pad %63s %u",button,&held)>=1){char script[128];std::snprintf(script,sizeof(script),"%u:%s:%u",field+2,button,held);PortInput_LoadScript(script);}
        }else if(line=="shot"){
            auto folder=std::string(root)+"/screenshots";
            auto* window=aurora_get_window();void* hwnd=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
            PortXD_ScreenshotTo(hwnd,folder.c_str(),field,picture);
        }
    }
}
