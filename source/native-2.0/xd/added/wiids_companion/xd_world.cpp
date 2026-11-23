#include "xd_companion.h"
#include <game/pxdvs/app/hero/hero.hpp>
#include <game/pxdvs/GSAPI/GSflag/flagData.h>
static_assert(sizeof(u32)==4,"GameCube u32 must remain 32-bit on LP64");
static_assert(sizeof(Pokemon)==0xc4,"Unexpected Pokemon layout");
static_assert(sizeof(DarkPokemon)==0x48,"Unexpected Shadow record layout");
class DeckDarkPokemon { public: bool isScenarioPokemon() const; u8 getWorth() const; };

extern "C" {
u8 fightIsFight(void);
s32 GSfloorGetCurrent(void);
u32 GSflagGet(u32);
void* savedataGetStatus(void*,s32);
u32 deckGetDarkPokemonSize(void);
DeckDarkPokemon* deckGetDeckDarkPokemon(u32);
u16 pokemonBiosGetDarkpokemonDataId(Pokemon*);
u32 floorGetPlaceName(u32);
const uint16_t* GSmsgGetGSchar(u32);
void* fightTrainerGetValidFightPokemonPtr(void*,u16);
Pokemon* fightPokemonGetPokemonPtr(void*);
Pokemon* fightOutPokemonGetPokemonPtr(void*);
void WiidsXD_ReadObjective(XdSnapshot*);
extern FlagData* flag_data;
extern u32* flag_data_number;
extern FlagLevel* flag_level;
s16 fightTrainerGetFightOutPokemonToTemotiBanme(void*,void*);
}

static void copyName(uint16_t* out,const uint16_t* text) {
    if(!text)return;
    for(int i=0;i<XD_TEXT-1&&text[i];i++)out[i]=text[i];
}
extern "C" void WiidsXD_CopyPokemonValue(void* raw,XdPokemon* out) {
    *out={};out->heart=out->heart_max=-1;
    auto* p=(Pokemon*)raw;
    if(!p||!p->getPokemonDataId())return;
    out->species=p->getPokemonDataId();out->hp=p->getHp();out->max_hp=p->getMaxHp();
    out->status=p->getCondition();out->shadow_id=pokemonBiosGetDarkpokemonDataId(p);
    copyName(out->name,reinterpret_cast<const uint16_t*>(p->getNicknamePtr()));
    if(p->isDarkPokemon())if(DarkPokemon* dark=p->getDarkPokemon()) {
        out->heart=dark->getDarkPointDirect();out->heart_max=dark->getInitDarkPointDirect();
    }
}
extern "C" void WiidsXD_BattleValues(void* trainer,void* actor,void* target,XdSnapshot* out) {
    out->actor_party_slot=-1;out->target_index=-1;
    if(!actor||!trainer)return;
    for(u16 i=0;i<6;i++) {
        void* fp=fightTrainerGetValidFightPokemonPtr(trainer,i);
        WiidsXD_CopyPokemonValue(fp?fightPokemonGetPokemonPtr(fp):nullptr,&out->party[i]);
    }
    WiidsXD_CopyPokemonValue(fightOutPokemonGetPokemonPtr(actor),&out->acting);
    out->actor_party_slot=fightTrainerGetFightOutPokemonToTemotiBanme(trainer,actor);
    if(target)WiidsXD_CopyPokemonValue(fightOutPokemonGetPokemonPtr(target),&out->targeted);
}

// Called on the native game/menu thread. No speculative memory-address reads.
extern "C" void WiidsXD_ReadWorld(XdSnapshot* out) {
    out->battle=fightIsFight()!=0;
    out->map_id=GSfloorGetCurrent();
    out->objective_known=0;
    // Menu daemon runs during boot before common.rel installs these live tables.
    // GSflagGet dereferences flag_data and flag_level before its own null-bits guard.
    if(!flag_data||!flag_data_number||*flag_data_number<=0x3c4||!flag_level||
       !flag_level[flag_data[0x3c4].level].bits)return;
    out->story_progress=GSflagGet(0x3c4);
    if(!savedataGetStatus(nullptr,2))return;
    Hero* hero=Hero::getHeroPtr();
    if(!hero)return;
    WiidsXD_ReadObjective(out);
    if(out->map_id<0x10000) {
        u32 name=floorGetPlaceName(out->map_id);
        if(name)copyName(out->map_name,GSmsgGetGSchar(name));
    }
    for(int i=0;i<6;i++) {
        auto& dst=out->party[i];Pokemon* p=hero->getPokemon(i);
        dst.heart=dst.heart_max=-1;
        if(!p||p->getPokemonDataId()==0)continue;
        dst.species=p->getPokemonDataId();dst.hp=p->getHp();dst.max_hp=p->getMaxHp();
        dst.status=p->getCondition();dst.shadow_id=pokemonBiosGetDarkpokemonDataId(p);
        copyName(dst.name,reinterpret_cast<const uint16_t*>(p->getNicknamePtr()));
        if(p->isDarkPokemon())if(DarkPokemon* d=p->getDarkPokemon()) {
            dst.heart=d->getDarkPointDirect();dst.heart_max=d->getInitDarkPointDirect();
        }
    }
    if(!savedataGetStatus(nullptr,0xf))return;
    uint32_t count=deckGetDarkPokemonSize();
    if(count>XD_MAX_SHADOW)count=XD_MAX_SHADOW;
    for(uint32_t i=1;i<count;i++) {
        DarkPokemon* d=darkPokemonGetDarkPokemon(i);
        DeckDarkPokemon* deck=deckGetDeckDarkPokemon(i);
        if(!d||!deck||!deck->isScenarioPokemon()||d->getStatus()==0)continue;
        auto& dst=out->shadows[out->shadow_count++];
        dst.id=i;dst.species=d->getPokemonID();dst.snagged=d->isSnach();
        // Observed/escaped (status 2), as distinct from a species never encountered.
        dst.missed=d->getStatus()==2&&!d->isSnach()&&deck->getWorth()&&d->getCatchFloorId()!=0xb5;
        dst.heart=d->getDarkPointDirect();
    }
}
