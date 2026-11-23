// Included at the end of menuFight.cpp; uses its native WazaAlloc/WazaWork definitions.
#include "xd_companion.h"
#include "xd_chain.h"
#include "xd_move_policy.h"
extern "C" uint8_t wazaCheckDarkWaza(uint16_t);
extern "C" void fightOutPokemonToMenuWazaStatus(void*,MenuWazaStatus*);
extern "C" int WiidsXD_SnagAvailable(void*,void*);
extern "C" void WiidsXD_BattleValues(void*,void*,void*,XdSnapshot*);
extern "C" void WiidsXD_ReadBattleEffectiveness(void*,void*,XdSnapshot*);
extern "C" uint16_t WiidsXD_ResolveBattleMoveType(void*,void*,uint16_t,uint16_t);
extern "C" uint32_t zokuseiBiosGetName(uint16_t);
static void* xdActor=nullptr;
static FIGHT_TRAINER* xdTrainer=nullptr;
static uint32_t xdActorSerial=0,xdActorToken=0;
static int xdIntent=0;
extern "C" void WiidsXD_TouchIntent(int action) { if(action)xdIntent=action>0?action:0; }
extern "C" int WiidsXD_CurrentSnagAvailable() { return WiidsXD_SnagAvailable(xdTrainer,xdActor); }
extern "C" void WiidsXD_ReadCurrentBattle(XdSnapshot* frame) {
    if(!frame->battle)return;
    frame->actor=xdActorToken;WiidsXD_BattleValues(xdTrainer,xdActor,nullptr,frame);
}
extern "C" void WiidsXD_SetBattleActor(void* trainer,void* actor,uint16_t) {
    WiidsXD_ChainCancel();
    xdTrainer=(FIGHT_TRAINER*)trainer;xdActor=actor;
    xdIntent=0;
    xdActorToken=trainer?++xdActorSerial:0;
}
extern "C" int WiidsXD_BindMissingActor(void* trainer,void* actor) {
    if(xdTrainer)return 0;
    WiidsXD_SetBattleActor(trainer,actor,0);return 1;
}
static void xdBattleChoice(XdSnapshot* frame,int action,int argument,int item,int cursor,bool legal,const uint16_t* label=nullptr) {
    if(frame->choice_count>=XD_MAX_CHOICES)return;
    auto& choice=frame->choices[frame->choice_count++];
    choice.action=action;choice.argument=argument;choice.item_id=item;choice.cursor=cursor;choice.enabled=legal;
    if(label)for(int i=0;i<XD_TEXT-1&&label[i];++i)choice.label[i]=label[i];
}
static int xdBattleCursor(XdSnapshot* f,int item) {
    for(uint32_t i=0;i<f->choice_count;++i)if(f->choices[i].action==XD_SELECT&&f->choices[i].item_id==item&&f->choices[i].enabled)return f->choices[i].cursor;
    return -1;
}
extern "C" int WiidsXD_BattlePartyLegal(int index) {
    if(!xdTrainer||index<0||index>=6)return 0;
    if(fightFloorCheckFightActionFightOutPokemonIrekaeSelect(nullptr,xdActor,nullptr)!=0)return 0;
    void* pokemon=fightTrainerGetValidFightPokemonPtr(xdTrainer,(u16)index);
    return pokemon&&fightTrainerCheckCanIrekaeFightPokemon(xdTrainer,pokemon)==0;
}
extern "C" void WiidsXD_ReadBattleWindow(void* raw,XdSnapshot* frame) {
    auto* window=(tagWINDOW_WORK*)raw;
    if(!window||window->state!=2)return;
    frame->actor=xdActorToken;
    void* selectedTarget=nullptr;
    // The target menu's selectable flags are the game's current target legality mask.
    if(window->menuID==0x5c) {
        auto* targets=(TargetArg*)windowGetParam(window,0);
        if(targets)for(uint32_t c=0;c<frame->choice_count;++c) {
            auto& choice=frame->choices[c];
            for(int i=0;i<4;i++)if(targetItemID(targets->entry[i].menuId)==choice.item_id) {
                const uint16_t* name=(const uint16_t*)targets->entry[i].unk8;
                if(name)for(int n=0;n<XD_TEXT-1&&name[n];++n)choice.label[n]=name[n];
                if(menuGetCursorItemID(0x5c)==choice.item_id)frame->target=targets->entry[i].menuId;
                if(xdIntent==XD_SNAG&&targets->count==2&&i<2) {
                    void* target=fightTargetGetPtrAsNowFightType((u16)(0xf+i),xdActor);
                    Pokemon* pokemon=target?(Pokemon*)fightOutPokemonGetPokemonPtr(target):nullptr;
                    if(!pokemon||!pokemon->isDarkPokemon())choice.enabled=0;
                }
            }
        }
        bool currentLegal=false;
        for(uint32_t c=0;c<frame->choice_count;c++)if(frame->choices[c].action==XD_SELECT&&frame->choices[c].item_id==menuGetCursorItemID(0x5c))currentLegal=frame->choices[c].enabled;
        if(!currentLegal)for(uint32_t c=0;c<frame->choice_count;c++)if(frame->choices[c].action==XD_CONFIRM)frame->choices[c].enabled=0;
        const u16 relative[]={0xc,0xe,0xf,0x10};
        if(xdActor&&frame->target)for(int i=0;i<4;i++) {
            void* candidate=fightTargetGetPtrAsNowFightType(relative[i],xdActor);
            if(candidate&&fightMenuGetFightOutPokemonPtrToStatusMenuId(candidate,fightFloor_GetFightType(nullptr),false)==frame->target) {
                selectedTarget=candidate;frame->target_index=i;break;
            }
        }
    }
    // A single live opponent is unambiguous even for moves that skip a target
    // prompt. In doubles, keep the native cursor as the sole target authority.
    if(!selectedTarget&&xdActor&&window->menuID!=0x5c) {
        void* only=nullptr;int count=0,index=-1;
        for(int i=0;i<2;i++) {
            void* p=fightTargetGetPtrAsNowFightType((u16)(0xf+i),xdActor);
            if(p&&fightOutPokemonCheckFightOut(p)){only=p;count++;index=i+2;}
        }
        if(count==1){selectedTarget=only;frame->target_index=index;
            frame->target=fightMenuGetFightOutPokemonPtrToStatusMenuId(only,fightFloor_GetFightType(nullptr),false);}
    }
    int targetIndex=frame->target_index;
    WiidsXD_BattleValues(xdTrainer,xdActor,selectedTarget,frame);
    if(selectedTarget)frame->target_index=targetIndex;
    bool command=window->menuID==0x3a&&!isImasugu()&&!fightFloorIsNowEncountTuusinTaisen(nullptr);
    bool ordinaryEscape=command&&
        !fightKind_CheckFightKind(0xd)&&!fightKind_CheckFightKind(0xe)&&!fightKind_CheckFightKind(0xf)&&
        !fightKind_CheckFightKind(6)&&!fightKind_CheckFightKind(7);
    bool moveMenu=window->menuID==0x39;
    bool contextMoves=!moveMenu;
    if(contextMoves&&!xdActor)return;
    MenuWazaStatus status={};
    if(contextMoves)fightOutPokemonToMenuWazaStatus(xdActor,&status);
    auto* alloc=contextMoves?(WazaAlloc*)&status:(WazaAlloc*)windowGetAllocPtr(window);
    auto* work=contextMoves?nullptr:(WazaWork*)windowGetFreeWork(window);
    if(!alloc||!alloc->fightOut||(moveMenu&&!work))return;
    Pokemon* pokemon=(Pokemon*)fightOutPokemonGetPokemonPtr(alloc->fightOut);
    if(!pokemon)return;
    // initKeep=0: this read must not reset the game's Encore state.
    for(int i=0;i<4;i++) {
        auto& dst=frame->moves[i];auto& src=alloc->rows[i];
        dst.id=pokemonGetStatus(pokemon,0,0x7f,i);dst.type=src.type;dst.pp=src.pp;dst.max_pp=src.ppMax;
        dst.legal=WiidsXD_MoveSelectable((uint16_t)dst.id,(contextMoves||work->state==0)&&src.name&&fightOutPokemonCheckCanOutOkWazaBanme(alloc->fightOut,i,0,nullptr)==0);
        if(src.name){const uint16_t* text=(const uint16_t*)src.name;for(int n=0;n<XD_TEXT-1&&text[n];n++)dst.name[n]=text[n];}
        if(src.unk4){const uint16_t* text=(const uint16_t*)src.unk4;for(int n=0;n<XD_TEXT-1&&text[n];n++)frame->move_type_names[i][n]=text[n];}
        const uint16_t resolved=WiidsXD_ResolveBattleMoveType(alloc->fightOut,pokemon,(uint16_t)dst.id,(uint16_t)dst.type);
        if(resolved!=dst.type){
            dst.type=resolved;auto* out=frame->move_type_names[i];for(int n=0;n<XD_TEXT;n++)out[n]=0;
            const auto* text=(const uint16_t*)GSmsgGetGSchar(zokuseiBiosGetName(resolved));
            if(text)for(int n=0;n<XD_TEXT-1&&text[n];++n)out[n]=text[n];
        }
        // Native Shadow rows omit the ordinary type icon; its stored base type
        // is not the move's displayed battle category.
        if(wazaCheckDarkWaza((uint16_t)dst.id)==1){
            auto* text=frame->move_type_names[i];for(int n=0;n<XD_TEXT;n++)text[n]=0;
            const char* label="SHADOW";for(int n=0;label[n];n++)text[n]=(uint16_t)label[n];
        }
        if(dst.id==0x176){
            dst.type=dst.pp=dst.max_pp=-1;
            for(int n=0;n<XD_TEXT;n++)frame->move_type_names[i][n]=0;
        }
        if(command) {
            int cursor=xdBattleCursor(frame,0x12c);
            xdBattleChoice(frame,XD_MOVE,i,0x12c,cursor,dst.legal&&cursor>=0,dst.name);
        }
        for(uint32_t c=0;c<frame->choice_count;c++) {
            auto& choice=frame->choices[c];
            if((choice.action==XD_MOVE&&choice.argument==i)||
               (choice.action==XD_SELECT&&choice.item_id==0x110+i*2))choice.enabled=dst.legal;
            if(moveMenu&&choice.action==XD_CONFIRM&&window->cursor==i)choice.enabled=dst.legal;
        }
    }
    WiidsXD_ReadBattleEffectiveness(xdActor,selectedTarget,frame);
    if(command) {
        int partyCursor=xdBattleCursor(frame,0x130);
        for(int i=0;i<6;i++)xdBattleChoice(frame,XD_PARTY_MEMBER,i,0x130,partyCursor,
            partyCursor>=0&&WiidsXD_BattlePartyLegal(i),frame->party[i].name);
        auto* data=(u8*)windowGetAllocPtr(window);
        if(data&&data[0x16]!=0&&fightFloorGetStatus(nullptr,0,0x20,0)!=0) {
            int bag=xdBattleCursor(frame,0x12e);
            xdBattleChoice(frame,XD_BAG,0,0x12e,bag,bag>=0);
            xdBattleChoice(frame,XD_SNAG,0,0x12e,bag,bag>=0&&WiidsXD_CurrentSnagAvailable());
        }
        if(ordinaryEscape&&data&&fightFloorGetStatus(nullptr,0,0x22,0)!=0) {
            int runItem=data[0x16]?0x132:0x12e;
            int run=xdBattleCursor(frame,runItem);
            xdBattleChoice(frame,XD_RUN,0,runItem,run,run>=0);
        }
    }
}
