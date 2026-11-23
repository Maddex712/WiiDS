#include "xd_companion.h"
#include <game/pxdvs/app/hero/hero.hpp>
extern "C" {
void* fightTrainer_GetHeroPtr(void*);
void* heroItemGetItemKindToItemAryPtr(Hero*,u8,u16*,u16*,u8*,u8*);
u8 itemCheckValid(void*);
u16 itemBiosGetItemDataId(void*);
u16 itemBiosGetNum(void*);
u8* itemDataBiosGetPtr(u16);
u32 itemDataBiosGetBattleUseFunc(u8*);
u8 fightTrainerIsSelectedItemBall(void*);
u32 fightFloor_GetStockPokemonNum(void*);
s8 pcboxGetNbPokemonBox(void);
s8 pcboxGetPokemonBoxNbEmptySlot(void*,s8);
void* fightTargetGetPtrAsNowFightType(u16,void*);
u8 fightOutPokemonCheckFightOut(void*);
Pokemon* fightOutPokemonGetPokemonPtr(void*);
}
// Return the first owned usable Ball, preferring the ordinary Poke Ball. No inventory write.
extern "C" int WiidsXD_OwnedSnagBall(void* trainer) {
    Hero* hero=(Hero*)fightTrainer_GetHeroPtr(trainer);
    if(!hero)return 0;
    u16 count=0;auto* entries=(u8*)heroItemGetItemKindToItemAryPtr(hero,1,&count,nullptr,nullptr,nullptr);
    if(!entries||count>256)return 0;
    int first=0;
    // Item inventory entries are the four-byte native records used by _getItemIDFromMenuPos.
    for(u16 i=0;i<count;i++)if(itemCheckValid(entries+i*4)&&itemBiosGetNum(entries+i*4)>0) {
        u16 id=itemBiosGetItemDataId(entries+i*4);
        if(!itemDataBiosGetBattleUseFunc(itemDataBiosGetPtr(id)))continue;
        if(id==1)return id;
        if(!first)first=id;
    }
    return first;
}
extern "C" int WiidsXD_SnagAvailable(void* trainer,void* actor) {
    if(!trainer||!actor||fightTrainerIsSelectedItemBall(trainer))return 0;
    Hero* hero=(Hero*)fightTrainer_GetHeroPtr(trainer);
    if(!hero||hero->getHeroStyle()!=2||!WiidsXD_OwnedSnagBall(trainer))return 0;
    int empty=0;
    for(int i=0;i<6;i++){Pokemon* p=hero->getPokemon(i);if(!p||p->getPokemonDataId()==0)++empty;}
    int boxes=pcboxGetNbPokemonBox();
    if(boxes<0||boxes>64)return 0;
    for(int i=0;i<boxes;i++)empty+=pcboxGetPokemonBoxNbEmptySlot(nullptr,(s8)i);
    if(empty<(int)fightFloor_GetStockPokemonNum(nullptr)+1)return 0;
    for(u16 relative=0xf;relative<=0x10;relative++) {
        void* target=fightTargetGetPtrAsNowFightType(relative,actor);
        if(!target||!fightOutPokemonCheckFightOut(target))continue;
        Pokemon* p=fightOutPokemonGetPokemonPtr(target);
        if(p&&p->isDarkPokemon())return 1;
    }
    return 0;
}
