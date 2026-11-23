#include "xd_chain.h"
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <atomic>
namespace {
std::atomic<void(*)(const char*,int,int,int)> traceSink{nullptr};
void trace(const char* event,int k,int menu=0,int state=0){
    if(auto sink=traceSink.load(std::memory_order_relaxed))sink(event,k,menu,state);
}
std::mutex chainLock;
int kind=0,argument=0,stage=0;
uint32_t actor=0;
uint64_t started=0;
bool choice(const XdSnapshot& f,int action,int arg,XdRequest* r) {
    for(uint32_t i=0;i<f.choice_count;++i) if(f.choices[i].enabled&&f.choices[i].action==action&&f.choices[i].argument==arg) {
        *r={f.epoch,action,arg}; return true;
    }
    return false;
}
}
extern "C" void WiidsXD_SetChainTrace(void(*sink)(const char*,int,int,int)){traceSink.store(sink,std::memory_order_relaxed);}
void WiidsXD_ChainBegin(int action,int arg,uint32_t owner,int sourceMenu) {
    std::lock_guard<std::mutex> guard(chainLock);
    if(WiidsXD_InputBlocked()){kind=0;return;}
    kind=action;argument=arg;actor=owner;stage=sourceMenu==0x4c?2:(sourceMenu==0x2c?1:0);started=WiidsXD_Now();
    trace("begin",kind,sourceMenu,stage);
}
void WiidsXD_ChainCancel() { std::lock_guard<std::mutex> guard(chainLock); if(kind)trace("cancel",kind);kind=0; }
int WiidsXD_ChainStep(const XdSnapshot* f,int physical,XdRequest* r) {
    std::lock_guard<std::mutex> guard(chainLock);
    if(!kind||!f||!r) return 0;
    if(WiidsXD_InputBlocked()){kind=0;return 0;}
    if(physical||WiidsXD_Now()-started>5000||(!actor&&f->battle)) {trace("interrupt",kind,f->menu_id,physical);kind=0;return 0; }
    if(!f->available) return 0; // native close/open animation; no timed input
    if(actor&&f->actor!=actor) {trace("actor changed",kind,f->menu_id,f->actor);kind=0;return 0;}
    const int menu=f->menu_id;
    // The native battle info window temporarily becomes active after the top
    // commands close and before the selected sub-menu opens. It is not a new
    // choice in the chain; keep waiting without injecting any key.
    if(actor&&(menu==0x37||menu==0x38||menu==0x40||menu==0x41))return 0;
    int action=0,arg=0;
    if(kind==XD_MOVE) {
        if(menu==0x3a||menu==0x8e) return 0;
        if(menu!=0x39&&menu!=0x90) {trace("unexpected move menu",kind,menu);kind=0;return 0; }
        action=XD_MOVE;arg=argument;
    } else if(kind==XD_SNAG) {
        if(menu==0x3a)return 0;
        if(menu==0x2c&&stage==0){action=XD_SNAG;stage=1;}
        else if(menu==0x2d&&stage==1){action=XD_SNAG;}
        else {kind=0;return 0;}
    } else if(kind==XD_PARTY_MEMBER) {
        if(menu==0x3a||menu==0x8e) return 0;
        if(menu==0x57||menu==0x58) { action=XD_PARTY;stage=1; }
        else if(menu==0x4c&&stage<2) { action=XD_PARTY_MEMBER;arg=argument;stage=2; }
        else if(menu==0x92&&stage<2) { action=XD_PARTY_MEMBER;arg=argument;stage=2; }
        else if((menu==0x50||menu==0x51||menu==0x4f||menu==0x5a)&&stage==2) {
            // Native menu result zero is Switch (battle) or Summary (field).
            // The item identifier is data-owned and differs between these menus.
            action=XD_SELECT;bool found=false;
            for(uint32_t i=0;i<f->choice_count;i++)if(f->choices[i].action==XD_SELECT&&f->choices[i].cursor==0&&f->choices[i].enabled){arg=f->choices[i].argument;found=true;break;}
            if(!found){kind=0;return 0;}
        }
        else { kind=0;return 0; }
    } else if(kind==XD_BAG||kind==XD_PARTY||kind==XD_PDA) {
        if(menu!=0x57&&menu!=0x58) { kind=0;return 0; }
        action=kind;
    } else { kind=0;return 0; }
    if(!choice(*f,action,arg,r)) {trace("choice unavailable",kind,menu);kind=0;return 0; }
    trace("step",kind,menu,stage);
    if(!(kind==XD_PARTY_MEMBER&&(menu==0x57||menu==0x58||menu==0x4c))&&!(kind==XD_SNAG&&menu==0x2c)) kind=0;
    return 1;
}
