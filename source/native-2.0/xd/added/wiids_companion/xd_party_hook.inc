// Included after menuPokemon.cpp's native definitions. The menu's own slot mapping remains authoritative.
#include "xd_companion.h"
extern "C" int WiidsXD_BattlePartyLegal(int);
extern "C" int WiidsXD_PartyChainAllowed() { return _menuPokemon.kind==1||_menuPokemon.kind==2; }
extern "C" void WiidsXD_ReadPartyWindow(void* raw,XdSnapshot* frame) {
    auto* window=(tagWINDOW_WORK*)raw;
    if(!window||window->menuID!=0x4c||window->state!=2||_menuPokemon.listKind!=0)return;
    if(_menuPokemon.canCancel==0||winSeqCheckMove(0x4b)) {
        for(uint32_t i=0;i<frame->choice_count;++i)frame->choices[i].enabled=0;
        return;
    }
    if(_menuPokemon.f19==0)for(uint32_t i=0;i<frame->choice_count;++i) {
        auto& c=frame->choices[i];
        if(c.action==XD_CANCEL||(c.action==XD_CONFIRM&&menuGetCursorItemID(0x4c)==0x162))c.enabled=0;
    }
    for(int i=0;i<6&&frame->choice_count<XD_MAX_CHOICES;++i) {
        auto& c=frame->choices[frame->choice_count++];
        c.action=XD_PARTY_MEMBER;c.argument=i;c.cursor=i;c.item_id=menu_info[(s8)_menuPokemon.side][i].item;
        // Item use/give and other native selectors return the chosen slot
        // directly. Only the normal battle Switch menu needs switch legality.
        c.enabled=_menuPokemonGetPokemonPtr(i)!=nullptr&&(_menuPokemon.kind!=2||WiidsXD_BattlePartyLegal(i));
        for(int n=0;n<XD_TEXT-1&&frame->party[i].name[n];++n)c.label[n]=frame->party[i].name[n];
    }
}
