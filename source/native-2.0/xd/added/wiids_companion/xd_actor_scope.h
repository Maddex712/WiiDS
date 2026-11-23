#pragma once
#include <stdint.h>
extern "C" void WiidsXD_SetBattleActor(void*,void*,uint16_t);
extern "C" int WiidsXD_BindMissingActor(void*,void*);
struct WiidsXDBattleScope { ~WiidsXDBattleScope(){WiidsXD_SetBattleActor(nullptr,nullptr,0);} };
struct WiidsXDConditionalActorScope {
    bool bound;
    WiidsXDConditionalActorScope(void* trainer,void* actor):bound(WiidsXD_BindMissingActor(trainer,actor)!=0){}
    ~WiidsXDConditionalActorScope(){if(bound)WiidsXD_SetBattleActor(nullptr,nullptr,0);}
};
