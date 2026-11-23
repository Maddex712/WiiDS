#include <dolphin/types.h>
#include "game/portxd_xg.h"
#include <game/gcptr.h>
#include <game/pcret.h>
#include <game/statusval.h>
#include <game/fightseqscript.h>

#pragma optimize_for_size on

struct FightOutPokemon;
struct FightPokemon;
struct FightTrainer;
struct Pokemon;

struct LvupStatus {
    u16 data[7];
};

static s32 _WsScaEndSetSub(void*, u16, void*);

extern "C" {

extern u32 ServerStatusFlag;
extern u8 ServerWork[8];
extern u16 PushWazaNo;
extern FightPokemon* get_exp_fight_pokemon_ptr;
extern LvupStatus old_menu_lvup_status;
#ifdef __MWERKS__
extern void* WazaEffectTbl[];
#endif

void* fightSeq_GetWazaSeq(void);
void fightSeq_NextWazaSeq(s32 size);
void fightSeq_JumpWazaSeq(void* target);
void fightSeqDamageLoss(u32, u32);
FightOutPokemon* fightTargetGetPtrAsNowFightType(u32, u32);
u8 fightOutPokemonIsJoutaiDataId(FightOutPokemon*, u32);
u32 fightOutPokemon_GetTokuseiDataId(FightOutPokemon*);
FightPokemon* fightOutPokemon_GetFightPokemonPtr(FightOutPokemon*);
void* fightFloorGetFightPokemonPtrToFightTrainerPtr(s32, void*);
u32 fightFloor_GetFightType(s32);
void* fightTargetGetPtr(u32 kind, void* a, PCARG(u8, u16) b);
s32 fightPokemon_GetEntryId(FightPokemon*);
u16 fightTrainer_GetFightTrainerDataId(FightTrainer*);
u16 fightTrainerDB_GetAiDataId(u16);
u8 fightTrainerAiDB_GetParamStoreFlag(u16);
void fightTrainer_SetEnemyTokuseiFlag(void*, s32, u32);
void fightTrainer_SetEnemyStoreTokuseiData(void*, s32, u32);
u32 fightOutPokemon_GetUseWazaDataId(FightOutPokemon*);
void fightOutPokemonInitJoutaiKeep(FightOutPokemon*, u32);
void fightFloor_SetAttackPokemonPtr(s32, FightOutPokemon*);
FightOutPokemon* fightSeqGetEnemyFightSideKonoyubitomareFightOutPokemonPtr(FightOutPokemon*);
void fightFloor_SetDefPokemonAndAtkFightWazaTargetId(s32, FightOutPokemon*);
u8 EscapeNGCheck(FightOutPokemon*);
FightOutPokemon* fightSeqSelectDefensePokemonPtr(FightOutPokemon*, u32, u32, u32, u32, u32, s32);
u8 fightOutPokemon_GetKizetuFlag(FightOutPokemon*);
FightOutPokemon* fightTrainerCheckFightPokemonFightOut(void*, void*);
u32 fightOutPokemonGetIdBuffJoutaiDataId(FightOutPokemon*, u32);
Pokemon* fightOutPokemonGetPokemonPtr(FightOutPokemon*);
s8 pokemonSearchWazaDataId(Pokemon*, u32);
void fightOutPokemonInitJoutaiDataId(FightOutPokemon*, u32);
void fightMenuFightOutPokemonOpenStatusMenu(FightOutPokemon*, PCARG(u8, u16), u32);
void fightMenuFightOutPokemonCloseStatusMenuAnotherMine(s32, FightOutPokemon*, PCARG(u8, u16), u32, u32);
void fightOutPokemonSetVisibility(FightOutPokemon*, u32);
void* fightOutPokemon_GetSequencePtr(FightOutPokemon*);
void Sequence_ResetStatus(void*, u32);
void fightPokemonToMenuLvupStatus(FightPokemon*, LvupStatus*);
void fightMenuSubMenuLvupStatus(LvupStatus*, LvupStatus*, LvupStatus*);
void fightMenuOpenLevelUpStatusMenu(LvupStatus*, u32);
void fightMenuCloseLevelUpStatusMenu(void);
void fightOutPokemonDoItemSoubi(FightOutPokemon*, u32, u32, u32);
void fightSeqCheckSetHensinBuffToMotoPokemon(FightOutPokemon*, u32);
void fightSeqFightOutPokemonEffect(u32, FightOutPokemon*, u32, u32);
void fightOutPokemon_SetFightActionFlag(FightOutPokemon*, u32);
u8 fightFloorLoopValidFightOutPokemon(PCARG(u8, void*), s32 (*)(void*, u16, void*), void*, u8);
void fightFloorReplaceFightOutPokemonAttackToDefense(s32);
void* fightOutPokemon_GetFightWazaPtr(FightOutPokemon*);
u16 wazaDB_GetSeqId(u32);
void fightWazaSetUseWazaStatus(void*, u32);
void fightWaza_SetMotoWazaDataId(void*, u32);
FightPokemon* fightOutPokemon_GetMotoFightPokemonPtr(FightOutPokemon*);
Pokemon* fightPokemonGetPokemonPtr(FightPokemon*);
u32 pokemon_GetHp(Pokemon*);
void pokemon_SetHp(Pokemon*, u32);
void* fightOutPokemon_GetFightItemPtr(FightOutPokemon*);
u16 fightItem_GetItemDataId(void*);
u8 fightSeqGetItemType(u16, FightOutPokemon*);
struct ItemUseEntry;
s16 itemUse2Pokemon(ItemUseEntry*, Pokemon*, FightPokemon*, u16, u8);
void fightWaza_SetDamage(void*, s32);
void* fightFloorGetFightOutPokemonPtrToFightTrainerPtr(s32, FightOutPokemon*);
u8 fightOutPokemonIsGcHeroFightOutPokemon(FightOutPokemon*);
u8 fightFloorIsNowEncountTuusinTaisen(s32);
void* fightTrainerGetPrefixNamePtr(void*);
void* fightTrainerGetNamePtr(void*);
void* fightOutPokemonGetNicknamePtr(FightOutPokemon*);
void fightMsgctrlSetValue(u32, GCuptr);
s32 fightFloorGetEnemyFightSideManyDamageHpRate(s32, FightOutPokemon*);
s32 fightFloorGetEnemyFightSideLittleNokoriHpRate(s32, FightOutPokemon*);

extern u8 waza_oboe_banme;
extern u16 OboeWazaNo;
u32 fightFloor_GetFightTypeFightoutPokemonNum(s32);
u8 fightFloor_GetFightKindGetOkaneFlag(s32);
void* fightFloorGetGcHeroFightTrainerPtr(s32);
void* fightTrainer_GetHeroPtr(void*);
u8 fightTrainer_GetOkaneBai(void*);
void* fightTrainerKindDataBiosGetPtr(u16);
u16 fightTrainerKindDataBiosGetSyoukinBairitu(void*);
u8 fightTrainerDB_GetKindDataId(u16);
FightPokemon* fightTrainer_GetFightPokemonPtr(void*, u16);
u8 fightPokemonCheckValid(FightPokemon*);
u8 figthPokemonGetLevel(FightPokemon*);
u8 fightPokemon_GetCatchEntryFlag(FightPokemon*);
u8 fightFloor_GetFightKindOkanePoolFlag(s32);
s32 scriptAddPremium(s32);
void heroAddPokedoru(void*, s32);
StatusU32 heroGetStatus(void*, u32, u32);
u8 fightWazaIsHit(void*);
void fightOutPokemonDamageEffect(FightOutPokemon*);
u8 fightMenuYesNo(void);
void fightMenuCloseMsg(void);
s32 fightMenuWazaWasure(Pokemon*, u16);
u32 pokemon_GetPokemonWazaDataId(Pokemon*, PCARG(u16, u32));
u8 wazaDB_GetHidenFlag(u32);
u8 fightMenuOpenMsg(u32);
void pokemonWazaCreate(Pokemon*, u16, u16);
void* wazaDB_GetName(u32);
void* GSmsgGetGSchar(void*);
u8 pokemonGetOboeWazaDataBanme(Pokemon*, u8);
s8 pokemonOboeWaza(Pokemon*, u8, u32, u8*);
u16 pokemonGetOboeWazaDataId(Pokemon*, u8, u8*);
void* fightPokemonGetNicknamePtr(FightPokemon*);
u8 fightOutPokemonCheckFightOut(FightOutPokemon*);
void fightOutPokemonModosuEffect(FightOutPokemon*, u32);
void fightMenuFightOutPokemonCloseStatusMenu(FightOutPokemon*, PCARG(u8, u16), u32);
u16 fightOutPokemonGetVoiceSndId(FightOutPokemon*);
void GSsndPlayVolume(u32, u32, u32, u32);
void soundOpenPlayME(u32, u32, u32);
void fightFloorSetMeetEnemyFightPokemonEnemySideAll(s32);
void fightFloorRegistFightTrainerEnemyPokemonFightSideAll(s32);
void fightOutPokemon_SetIrekaetaFlag(FightOutPokemon*, u32);
void fightOutPokemon_SetIrekaeTargetEntryId(FightOutPokemon*, s16);
u8 fightSide_GetMakibisiCheckFlag(void*);
u8 fightSideIsJoutaiDataId(void*, u32);
u8 fightOutPokemonIsZokuseiDataId(FightOutPokemon*, u32);
void fightSide_SetMakibisiCheckFlag(void*, u32);
s16 fightSideGetCountAsJoutaiDataId(void*, u32);
u32 fightOutPokemonMaxHpWaruValue(FightOutPokemon*, u16);
void fightWazaBiosCopy(void*, void*);
void fightFloor_SetAppointPokemonPtr(s32, FightOutPokemon*);
void fightSeq_CallWazaSeq(void*);
void fightOutPokemon_SetNamakeFlag(FightOutPokemon*, u32);
void fightSeqFightOutPokemonDarkCheckAppear(FightOutPokemon*, u32);
void fightSeqFightOutPokemonTokuseiCheckAppear(FightOutPokemon*);
void fightSeqPokemonSoubiItemCheckAppear(FightOutPokemon*);
void fightFloorCreateFightPokemonEnemyAryEnemySideAll(s32);
void* fightOutPokemonGetNamePtr(FightOutPokemon*);
u8 fightOutPokemon_GetIrekaetaFlag(FightOutPokemon*);
void* fightTrainerSearchIrekaeFightPokemon(void*, u16*);
s16 fightTrainerSelectIrekaeFightPokemon(void*, PCARG(u8, u32), u32, FightOutPokemon*);
void fightFloorSetTuusinErrorFightResult(s32);
void fightFloor_SetIrekaePokemonPtr(s32, FightOutPokemon*);
u8 fightFloorCheckFightActionFightOutPokemonIrekaeSelect(s32, FightOutPokemon*, u32);
void* fightTrainer_GetFightoutPokemonBuffPtr(void*);
u32 fightFloor_GetEncountDataId(s32);
void fightTrainerBallThrowEffect(void*, FightOutPokemon*, u32);
void fightMenuFightOutPokemonOpenStatusMenuStart(FightOutPokemon*, PCARG(u8, u16), u32);
void fightOutPokemonDasuEffect(void*, FightOutPokemon*, u32);
s32 fightOutPokemonGetJoutaiMigawariHp(FightOutPokemon*);
void fightWazaWzxTypeFuncMigawari(u32, FightOutPokemon*, FightOutPokemon*, u32, u32);
u8 wazaSequenceSysLastWazaIsImpactPoint(s32);
void GSthreadSwitch(void);
void fightOutPokemonFreeAllSequenceWaza(FightOutPokemon*);
u8 fightFloor_GetFightKindDoZukanMitaFlag(s32);
void fightOutPokemonSetOnZukanFlag(FightOutPokemon*, u32);
u8 pokemonIsDarkPokemon(Pokemon*);
u32 pokemon_GetDarkpokemonDataId(Pokemon*);
void darkPokemonBiosSetFightFlag(u32, u32);
void fightOutPokemonInit(void*);
s32 fightOutPokemon_GetIrekaeTargetEntryId(FightOutPokemon*);
u8 fightPokemonIsJoutaiDataId(FightPokemon*, u32);
void fightPokemonSetCountAsJoutaiDataId(FightPokemon*, u32, u32);
FightPokemon* fightTrainerGetEntryIdToFightPokemonPtr(void*, s32);
void fightPokemonReplace(FightPokemon*, FightPokemon*);
void menuFaceLoadFightPokemon(FightPokemon*, u32, u32);
void* pokemonCreateSequence(FightPokemon*);
void fightOutPokemonCreate(FightOutPokemon*, FightPokemon*, void*);
void fightOutPokemonRegWzxLoad(FightOutPokemon*);
void fightSeqIrekaeInit(FightOutPokemon*, void*, u32);
void* fightTrainer_GetSequencePtr(void*);
void battleGridAddPokemon(void*, void*);
void battleGridUpdate(void);
void fightOutPokemonBiosCopy(void*, FightOutPokemon*);
void fightWazaInitJoutai(void*);
u32 fightOutPokemonGetTokuseiDataId(FightOutPokemon*);

void WS_TOKUSEI_SET(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    FightPokemon* fp = fightOutPokemon_GetFightPokemonPtr(p);
    void* trainer = fightFloorGetFightPokemonPtrToFightTrainerPtr(0, fp);
    void* trainerA = fightTargetGetPtr(9, trainer, fightFloor_GetFightType(0));
    void* trainerB = fightTargetGetPtr(0xa, trainer, fightFloor_GetFightType(0));
    s32 entry = fightPokemon_GetEntryId(fp);
    if (trainerA) {
        if (fightTrainerAiDB_GetParamStoreFlag(fightTrainerDB_GetAiDataId(fightTrainer_GetFightTrainerDataId((FightTrainer*)trainerA))) == 1) {
            fightTrainer_SetEnemyTokuseiFlag(trainerA, entry, 1);
            fightTrainer_SetEnemyStoreTokuseiData(trainerA, entry, fightOutPokemon_GetTokuseiDataId(p));
        }
    }
    if (trainerB) {
        if (fightTrainerAiDB_GetParamStoreFlag(fightTrainerDB_GetAiDataId(fightTrainer_GetFightTrainerDataId((FightTrainer*)trainerB))) == 1) {
            fightTrainer_SetEnemyTokuseiFlag(trainerB, entry, 1);
            fightTrainer_SetEnemyStoreTokuseiData(trainerA, entry, fightOutPokemon_GetTokuseiDataId(p));
        }
    }
    fightSeq_NextWazaSeq(2);
}

void WS_SUBROUTINE_CALL(void) {
    FightOutPokemon* def;
    FightOutPokemon* o;
    u32 id;
    u32 move;
    u8* cmd;
    FightOutPokemon* r;
    FightOutPokemon* p;
    u8 fightType;
    FightPokemon* fp;
    fightType = fightFloor_GetFightType(0);
    cmd = (u8*)fightSeq_GetWazaSeq();
    p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    move = fightOutPokemon_GetUseWazaDataId(p);
    switch (cmd[2]) {
    case 0:
        fightOutPokemonInitJoutaiKeep(p, move);
        break;
    case 1:
        fightTargetGetPtrAsNowFightType(0x11, 0);
        def = fightTargetGetPtrAsNowFightType(0x12, 0);
        fightFloor_SetAttackPokemonPtr(0, def);
        r = fightSeqGetEnemyFightSideKonoyubitomareFightOutPokemonPtr(def);
        if (r == 0) {
            r = p;
        }
        fightFloor_SetDefPokemonAndAtkFightWazaTargetId(0, r);
        break;
    case 2:
        ServerWork[0] = EscapeNGCheck(fightTargetGetPtrAsNowFightType(0x11, 0));
        break;
    case 3:
        r = fightSeqSelectDefensePokemonPtr(p, move, fightType, 0, 1, 1, -1);
        if (r) {
            fightFloor_SetDefPokemonAndAtkFightWazaTargetId(0, r);
        }
        break;
    case 4:
        if (fightOutPokemon_GetKizetuFlag(p) == 1) {
            ServerWork[0] = 1;
        } else {
            ServerWork[0] = 0;
        }
        break;
    case 5:
        break;
    case 6:
        if ((fp = get_exp_fight_pokemon_ptr)) {
            void* trainer = fightFloorGetFightPokemonPtrToFightTrainerPtr(0, fp);
            if (trainer) {
                o = fightTrainerCheckFightPokemonFightOut(trainer, fp);
                if (o) {
                    if (fightOutPokemonIsJoutaiDataId(o, 0x36) == 1) {
                        id = fightOutPokemonGetIdBuffJoutaiDataId(o, 0x36);
                        if (pokemonSearchWazaDataId(fightOutPokemonGetPokemonPtr(o), id) < 0) {
                            fightOutPokemonInitJoutaiDataId(o, 0x36);
                        }
                    }
                }
            }
        }
        break;
    case 7:
        fightMenuFightOutPokemonOpenStatusMenu(p, fightType, 1);
        fightMenuFightOutPokemonCloseStatusMenuAnotherMine(0, p, fightType, 0, 1);
        break;
    }
    fightSeq_NextWazaSeq(3);
}

struct ItemUseEntry {
    s32 id;
    s16 value;
    s16 pad;
};

void WS_STATUS_RECOVER(void) {
    ItemUseEntry entries[33];
    FightOutPokemon* p;
    u8 itemType;
    FightPokemon* fp;
    FightPokemon* moto;
    Pokemon* pk;
    void* waza;
    s16 count;
    u16 item;
    u16 i;
    s32 amount;
    s32 hp;
    void* seq;
    amount = 0;
    fightSeq_GetWazaSeq();
    p = fightTargetGetPtrAsNowFightType(0x11, 0);
    waza = fightOutPokemon_GetFightWazaPtr(p);
    fp = fightOutPokemon_GetFightPokemonPtr(p);
    moto = fightOutPokemon_GetMotoFightPokemonPtr(p);
    pk = fightPokemonGetPokemonPtr(fp);
    hp = pokemon_GetHp(pk);
    item = fightItem_GetItemDataId(fightOutPokemon_GetFightItemPtr(p));
    itemType = fightSeqGetItemType(item, p);
    seq = fightOutPokemon_GetSequencePtr(p);
    count = itemUse2Pokemon(entries, 0, moto, item, 0);
    if (itemType == 1 || itemType == 2) {
        for (i = 0; i < count; i++) {
            if (entries[i].id == 0x15) {
                amount = entries[i].value;
            }
        }
        pokemon_SetHp(pk, hp);
        amount = -amount;
        fightWaza_SetDamage(waza, amount);
    }
    for (i = 0; i < count; i++) {
        if (entries[i].id == 0xf) {
            Sequence_ResetStatus(seq, 2);
        }
        if (entries[i].id == 9) {
            Sequence_ResetStatus(seq, 1);
        }
    }
    fightSeq_NextWazaSeq(1);
}
#ifndef __MWERKS__
extern "C" int PortXD_MsgIsHeroSide(void* fop, int isHero);
#endif

void WS_POKE_HPDEC_RATE(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    void* trainer = fightFloorGetFightOutPokemonPtrToFightTrainerPtr(0, p);
    bool isHero = fightOutPokemonIsGcHeroFightOutPokemon(p) == 1;
#ifndef __MWERKS__
    isHero = PortXD_MsgIsHeroSide(p, isHero) != 0;
#endif
    bool isTusin = fightFloorIsNowEncountTuusinTaisen(0) == 1;
    if (fightTrainerGetPrefixNamePtr(trainer) == 0 && !isHero) {
        isTusin = 1;
    }
    fightMsgctrlSetValue(0x16, (GCuptr)fightOutPokemonGetNicknamePtr(p));
    fightMsgctrlSetValue(0xd, (GCuptr)fightOutPokemonGetNicknamePtr(p));
    fightMsgctrlSetValue(0x22, (GCuptr)fightTrainerGetPrefixNamePtr(trainer));
    fightMsgctrlSetValue(0x23, (GCuptr)fightTrainerGetNamePtr(trainer));
    fightMsgctrlSetValue(0x25, (GCuptr)fightTrainerGetNamePtr(trainer));
    if (isTusin == 1) {
        ServerWork[5] = 5;
    } else if (isHero == 1) {
        s32 rate = fightFloorGetEnemyFightSideManyDamageHpRate(0, p);
        if (rate < 0) {
            ServerWork[5] = 1;
        } else if (rate == 0) {
            ServerWork[5] = 0;
        } else if (rate < 30) {
            ServerWork[5] = 1;
        } else if (rate < 70) {
            ServerWork[5] = 2;
        } else {
            ServerWork[5] = 3;
        }
    } else {
        ServerWork[5] = 4;
    }
    fightSeq_NextWazaSeq(2);
}

void WS_POKE_HPMAX_RATE(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    void* trainer = fightFloorGetFightOutPokemonPtrToFightTrainerPtr(0, p);
    bool isHero = fightOutPokemonIsGcHeroFightOutPokemon(p) == 1;
#ifndef __MWERKS__
    isHero = PortXD_MsgIsHeroSide(p, isHero) != 0;
#endif
    bool isTusin = fightFloorIsNowEncountTuusinTaisen(0) == 1;
    if (fightTrainerGetPrefixNamePtr(trainer) == 0 && !isHero) {
        isTusin = 1;
    }
    fightMsgctrlSetValue(0x16, (GCuptr)fightOutPokemonGetNicknamePtr(p));
    fightMsgctrlSetValue(0xd, (GCuptr)fightOutPokemonGetNicknamePtr(p));
    fightMsgctrlSetValue(0x22, (GCuptr)fightTrainerGetPrefixNamePtr(trainer));
    fightMsgctrlSetValue(0x23, (GCuptr)fightTrainerGetNamePtr(trainer));
    fightMsgctrlSetValue(0x25, (GCuptr)fightTrainerGetNamePtr(trainer));
    if (isTusin == 1) {
        ServerWork[5] = 5;
    } else if (isHero == 1) {
        s32 rate = fightFloorGetEnemyFightSideLittleNokoriHpRate(0, p);
        if (rate >= 70 || rate < 0) {
            ServerWork[5] = 0;
        } else if (rate >= 40) {
            ServerWork[5] = 1;
        } else if (rate >= 10) {
            ServerWork[5] = 2;
        } else {
            ServerWork[5] = 3;
        }
    } else {
        ServerWork[5] = 4;
    }
    fightSeq_NextWazaSeq(2);
}

void WS_ESCAPE_CHECK(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(5);
}

void WS_TOKUSYUNO_SET(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(1);
}

void fightSeqVanishOff(FightOutPokemon* p);

void WS_VANISH_OFF(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    if (p) {
        fightSeqVanishOff(p);
    }
    fightSeq_NextWazaSeq(2);
}

void fightSeqVanishOff(FightOutPokemon* p) {
    fightOutPokemonSetVisibility(p, 1);
    void* seq = fightOutPokemon_GetSequencePtr(p);
    if (seq) {
        Sequence_ResetStatus(seq, 3);
    }
}

void WS_GET_RESHUFFLE_CLIENT(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(1);
}

void WS_EXPFLAG_CLEAR(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(1);
}

void WS_STATUS_WINDOW(void) {
    LvupStatus b;
    LvupStatus a;
    FightPokemon* fp = get_exp_fight_pokemon_ptr;
    fightSeq_GetWazaSeq();
    fightPokemonToMenuLvupStatus(fp, &b);
    fightMenuSubMenuLvupStatus(&b, &old_menu_lvup_status, &a);
    fightMenuOpenLevelUpStatusMenu(&a, 1);
    fightMenuOpenLevelUpStatusMenu(&b, 0);
    fightMenuCloseLevelUpStatusMenu();
    fightSeq_NextWazaSeq(1);
}

void WS_RESHUFFLE_NICKNAME(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(1);
}

void WS_KILLITEM(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    fightOutPokemonDoItemSoubi(p, 0, 0, 1);
    fightSeqCheckSetHensinBuffToMotoPokemon(p, 0x82);
    fightSeq_NextWazaSeq(2);
}

void WS_KORAERU_CHECK(void) {
    fightSeq_GetWazaSeq();
    fightSeqDamageLoss(1, 0);
    fightSeq_NextWazaSeq(1);
}

void WS_SCA_END_SET(void) {
    fightSeq_GetWazaSeq();
    fightFloorLoopValidFightOutPokemon(0, _WsScaEndSetSub, 0, 0);
    fightSeq_NextWazaSeq(1);
}

}

static s32 _WsScaEndSetSub(void* p, u16 id, void* user) {
    fightOutPokemon_SetFightActionFlag((FightOutPokemon*)p, 1);
    return 1;
}

extern "C" {

void WS_YES_NO_WINDOW(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(1);
}

void WS_STATUS_EFFECT_DIRECT(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    u32 kind = WS_RD32(cmd + 3);
    if (!(ServerStatusFlag & 0x80) && kind == 7) {
        fightSeqFightOutPokemonEffect(0x34, p, 0, 1);
    }
    fightSeq_NextWazaSeq(7);
}

void WS_STATUS_EFFECT2(void) {
    fightFloor_GetFightType(0);
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    u32 kind = WS_RD32(cmd + 2);
    if (!(ServerStatusFlag & 0x80)) {
        if (kind == 9) {
            fightSeqFightOutPokemonEffect(0x34, p, 0, 1);
        }
        if (kind == 0x18) {
            fightSeqFightOutPokemonEffect(0x35, p, 0, 1);
        }
        if (kind == 0x17) {
            fightSeqFightOutPokemonEffect(0x36, p, 0, 1);
        }
        if (kind == 10) {
            fightSeqFightOutPokemonEffect(0x37, p, 0, 1);
        }
    }
    fightSeq_NextWazaSeq(6);
}

void WS_STATUS_EFFECT(void) {
    fightFloor_GetFightType(0);
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    if (!(ServerStatusFlag & 0x80)) {
        if (fightOutPokemonIsJoutaiDataId(p, 8) == 1) {
            fightSeqFightOutPokemonEffect(0x2e, p, 0, 1);
        }
        if (fightOutPokemonIsJoutaiDataId(p, 5) == 1) {
            fightSeqFightOutPokemonEffect(0x2f, p, 0, 1);
        }
        if (fightOutPokemonIsJoutaiDataId(p, 7) == 1) {
            fightSeqFightOutPokemonEffect(0x30, p, 0, 1);
        }
        if (fightOutPokemonIsJoutaiDataId(p, 6) == 1) {
            fightSeqFightOutPokemonEffect(0x31, p, 0, 1);
        }
        if (fightOutPokemonIsJoutaiDataId(p, 3) == 1) {
            fightSeqFightOutPokemonEffect(0x32, p, 0, 1);
        }
        if (fightOutPokemonIsJoutaiDataId(p, 4) == 1) {
            fightSeqFightOutPokemonEffect(0x33, p, 0, 1);
        }
    }
    fightSeq_NextWazaSeq(2);
}

void WS_WAZASEQADRS(void) {
    fightFloor_GetFightType(0);
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    void* waza = fightOutPokemon_GetFightWazaPtr(fightTargetGetPtrAsNowFightType(0x11, 0));
    u32 wazaId = PushWazaNo;
    u16 seqId = wazaDB_GetSeqId(PushWazaNo);
    if (cmd[1] != 0) {
        fightWazaSetUseWazaStatus(waza, wazaId);
    } else {
        fightWaza_SetMotoWazaDataId(waza, wazaId);
        fightWazaSetUseWazaStatus(waza, wazaId);
    }
    fightSeq_JumpWazaSeq(WazaEffectTbl[seqId]);
}

void WS_BALL_GAUGE_OFF(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(2);
}

void WS_BALL_GAUGE_SET(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(2);
}

void WS_INC_RECORD(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(2);
}

void WS_SWITCH_A_D(void) {
    fightSeq_GetWazaSeq();
    fightFloorReplaceFightOutPokemonAttackToDefense(0);
    u32 flag = ServerStatusFlag;
    u32 next = flag | 0x1000;
    if (flag & 0x1000) {
        next = flag & 0xffffefff;
    }
    ServerStatusFlag = next;
    fightSeq_NextWazaSeq(1);
}

void WS_WAZA_GET(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(1);
}

void WS_OKODUKAI_CALC(void) {
    u16 syoukin;
    u8 okaneBai;
    u8* cmd;
    void* trainer;
    void* hero;
    u8 maxLevel;
    u8 fightoutNum;
    u16 mult;
    FightPokemon* fp1;
    void* enemy;
    FightPokemon* fp2;
    s32 j;
    s32 money;
    s32 i;
    cmd = (u8*)fightSeq_GetWazaSeq();
    fightoutNum = fightFloor_GetFightTypeFightoutPokemonNum(0);
    if (!fightFloor_GetFightKindGetOkaneFlag(0)) {
        fightSeq_JumpWazaSeq(WS_RDPTR(cmd + 1));
        return;
    }
    trainer = fightFloorGetGcHeroFightTrainerPtr(0);
    if (!trainer) {
        fightSeq_JumpWazaSeq(WS_RDPTR(cmd + 1));
        return;
    }
    hero = fightTrainer_GetHeroPtr(trainer);
    if (!hero) {
        fightSeq_JumpWazaSeq(WS_RDPTR(cmd + 1));
        return;
    }
    okaneBai = fightTrainer_GetOkaneBai(trainer);
    enemy = fightTargetGetPtrAsNowFightType(9, (u32)trainer);
    syoukin = fightTrainerKindDataBiosGetSyoukinBairitu(
        fightTrainerKindDataBiosGetPtr((u8)fightTrainerDB_GetKindDataId(fightTrainer_GetFightTrainerDataId((FightTrainer*)enemy))));
    maxLevel = 0;
    for (i = 0; i < 6; i++) {
        fp1 = fightTrainer_GetFightPokemonPtr(enemy, i);
        if (fightPokemonCheckValid(fp1)) {
            u8 level = figthPokemonGetLevel(fp1);
            if (maxLevel < level) {
                maxLevel = level;
            }
        }
    }
    for (j = 0; j < 6; j++) {
        fp2 = fightTrainer_GetFightPokemonPtr(trainer, j);
        if (fightPokemonCheckValid(fp2) && fightPokemon_GetCatchEntryFlag(fp2) == 1) {
            u8 level = figthPokemonGetLevel(fp2);
            if (maxLevel < level) {
                maxLevel = level;
            }
        }
    }
    if (!fightFloor_GetFightKindOkanePoolFlag(0)) {
        if (fightoutNum <= 1) {
            mult = 1;
        } else {
            mult = 2;
        }
    } else {
        mult = 4;
    }
    money = (u16)mult * ((u16)syoukin * (maxLevel * okaneBai));
    if (fightFloor_GetFightKindOkanePoolFlag(0) == 1) {
        scriptAddPremium(money);
        fightSeq_JumpWazaSeq(WS_RDPTR(cmd + 1));
    } else {
        heroAddPokedoru(hero, money);
        fightMsgctrlSetValue(0x2f, money);
        fightMsgctrlSetValue(0x4b, money);
        fightMsgctrlSetValue(0x13, heroGetStatus(hero, 1, 0));
        fightSeq_NextWazaSeq(5);
    }
}

void WS_POKE_BLINK(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    if (fightWazaIsHit(fightOutPokemon_GetFightWazaPtr(fightTargetGetPtrAsNowFightType(0x11, 0))) == 1) {
        u32 flag = ServerStatusFlag;
        if (flag & 0x100) {
            fightSeq_NextWazaSeq(2);
            return;
        }
        if (flag & 0x80) {
            fightOutPokemonDamageEffect(p);
        }
    }
    fightSeq_NextWazaSeq(2);
}

void WS_WAZAAKIRAME_CHECK(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    if (fightMenuYesNo() == 1) {
        waza_oboe_banme = waza_oboe_banme + 1;
        fightSeq_NextWazaSeq(5);
    } else {
        fightSeq_JumpWazaSeq(WS_RDPTR(cmd + 1));
    }
}

void WS_WAZAWASURE_CHECK(void) {
    u8* cmd;
    s32 slot;
    u32 wazaId;
    void* name;
    Pokemon* pokemon;
    pokemon = fightPokemonGetPokemonPtr(get_exp_fight_pokemon_ptr);
    cmd = (u8*)fightSeq_GetWazaSeq();
    if (fightMenuYesNo() == 1) {
        while (1) {
            fightMenuCloseMsg();
            slot = fightMenuWazaWasure(pokemon, OboeWazaNo);
            if (slot == -1 || slot >= 4) {
                break;
            }
            wazaId = pokemon_GetPokemonWazaDataId(pokemon, (u16)slot);
            if (XG_CONST(0, wazaDB_GetHidenFlag(wazaId)) == 1) {
                if (fightMenuOpenMsg(0x4f1e) == 1) {
                    fightMenuCloseMsg();
                }
                continue;
            }
            pokemonWazaCreate(pokemon, slot, OboeWazaNo);
            name = GSmsgGetGSchar(wazaDB_GetName(wazaId));
            fightMsgctrlSetValue(0xe, (GCuptr)name);
            fightMsgctrlSetValue(0x5d, 0x21);
            fightSeq_JumpWazaSeq(WS_RDPTR(cmd + 1));
            return;
        }
    }
    fightSeq_NextWazaSeq(5);
}

void WS_WAZAOBOE_CHECK(void) {
    void* jumpA;
    void* jumpB;
    FightPokemon* fp;
    u8 level;
    Pokemon* pokemon;
    u8* cmd;
    s8 result;
    fp = get_exp_fight_pokemon_ptr;
    level = figthPokemonGetLevel(fp);
    pokemon = fightPokemonGetPokemonPtr(fp);
    cmd = (u8*)fightSeq_GetWazaSeq();
    jumpA = WS_RDPTR(cmd + 1);
    jumpB = WS_RDPTR(cmd + 5);
    if (cmd[9] != 0) {
        waza_oboe_banme = pokemonGetOboeWazaDataBanme(pokemon, level);
    }
    while (1) {
        result = pokemonOboeWaza(pokemon, level, 0, &waza_oboe_banme);
        if (result == -1) {
            OboeWazaNo = pokemonGetOboeWazaDataId(pokemon, level, &waza_oboe_banme);
            fightSeq_NextWazaSeq(10);
            return;
        }
        if (result != -2) {
            break;
        }
        waza_oboe_banme = waza_oboe_banme + 1;
    }
    if (result == -3) {
        fightSeq_JumpWazaSeq(jumpB);
        return;
    }
    waza_oboe_banme = waza_oboe_banme + 1;
    OboeWazaNo = pokemon_GetPokemonWazaDataId(pokemon, result);
    fightSeq_JumpWazaSeq(jumpA);
}

void WS_OBOEWAZANO_SET(void) {
    FightPokemon* fp = get_exp_fight_pokemon_ptr;
    const GCuptr name = (GCuptr)GSmsgGetGSchar(wazaDB_GetName(OboeWazaNo));
    const GCuptr nick = (GCuptr)fightPokemonGetNicknamePtr(fp);
    fightSeq_GetWazaSeq();
    fightMsgctrlSetValue(0xd, nick);
    fightMsgctrlSetValue(0xe, name);
    fightSeq_NextWazaSeq(1);
}

void WS_POKEDELACTOR(void) {
    FightOutPokemon* p;
    u8 fightType;
    u8* cmd;
    fightType = fightFloor_GetFightType(0);
    cmd = (u8*)fightSeq_GetWazaSeq();
    p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    if (fightOutPokemonCheckFightOut(p) == 1) {
        fightOutPokemonModosuEffect(p, 0);
        fightMenuFightOutPokemonOpenStatusMenu(p, fightType, 1);
        fightMenuFightOutPokemonCloseStatusMenuAnotherMine(0, p, fightType, 0, 1);
        fightOutPokemonModosuEffect(p, 1);
        fightOutPokemonModosuEffect(p, 2);
        fightOutPokemonModosuEffect(p, 3);
        fightOutPokemonModosuEffect(p, 4);
        fightMenuFightOutPokemonCloseStatusMenu(p, fightType, 0);
    }
    fightSeq_NextWazaSeq(2);
}

void WS_END_FIGHT(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(1);
}

void WS_VOICE(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    GSsndPlayVolume(fightOutPokemonGetVoiceSndId(p), 0, 0xff, 0);
    fightSeq_NextWazaSeq(2);
}

void WS_ME_PLAY(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    soundOpenPlayME(WS_RD16(cmd + 1), 0, 0xff);
    fightSeq_NextWazaSeq(3);
}

void WS_SE_PLAY(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    GSsndPlayVolume(WS_RD16(cmd + 1), 0, 0xff, 0);
    fightSeq_NextWazaSeq(3);
}

void WS_TRAINER_APPEAR(void) {
    fightSeq_GetWazaSeq();
    fightSeq_NextWazaSeq(2);
}

struct FightWazaBuf {
    u8 data[0xac];
};

#ifdef __MWERKS__
extern u8 WAZA_MakibishiD[];
extern u8 WAZA_MakibishiA[];
extern u8 WAZA_MakibishiK[];
#endif

void WS_POKE_RESHUFFLE_END(void) {
    FightWazaBuf saved;
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    void* side = fightTargetGetPtrAsNowFightType(2, (u32)p);
    u32 tokusei = fightOutPokemonGetTokuseiDataId(p);
    fightFloorSetMeetEnemyFightPokemonEnemySideAll(0);
    fightFloorRegistFightTrainerEnemyPokemonFightSideAll(0);
    fightOutPokemon_SetIrekaetaFlag(p, 0);
    fightOutPokemon_SetIrekaeTargetEntryId(p, -1);
    if (!fightSide_GetMakibisiCheckFlag(side) && fightSideIsJoutaiDataId(side, 0x4a) == 1 &&
        !fightOutPokemonIsZokuseiDataId(p, 2) && (u16)tokusei != 0x1a) {
        u16 damage;
        void* waza;
        void* seq;
        fightSide_SetMakibisiCheckFlag(side, 1);
        ServerStatusFlag &= ~0x40;
        fightOutPokemonInitJoutaiDataId(p, 0x15);
        damage = fightOutPokemonMaxHpWaruValue(p, (u16)((5 - fightSideGetCountAsJoutaiDataId(side, 0x4a)) * 2));
        waza = fightOutPokemon_GetFightWazaPtr(fightTargetGetPtrAsNowFightType(0x11, 0));
        fightWazaBiosCopy(&saved, waza);
        fightWaza_SetDamage(waza, damage);
        fightWazaInitJoutai(waza);
        fightFloor_SetAppointPokemonPtr(0, p);
        if (cmd[1] == 0x12) {
            seq = WAZA_MakibishiD;
        } else if (cmd[1] == 0x11 || cmd[1] == 0x14) {
            seq = WAZA_MakibishiA;
        } else {
            seq = WAZA_MakibishiK;
        }
        fightSeq_CallWazaSeq(seq);
        fightWazaBiosCopy(waza, &saved);
        if (!fightSide_GetMakibisiCheckFlag(side)) {
            fightSeq_NextWazaSeq(2);
            return;
        }
    }
    if ((u16)fightOutPokemonGetTokuseiDataId(p) == 0x36) {
        fightOutPokemon_SetNamakeFlag(p, 1);
    }
    fightSeqFightOutPokemonDarkCheckAppear(p, 0);
    fightSeqFightOutPokemonTokuseiCheckAppear(p);
    fightSeqPokemonSoubiItemCheckAppear(p);
    fightSeqFightOutPokemonDarkCheckAppear(p, 1);
    fightFloorCreateFightPokemonEnemyAryEnemySideAll(0);
    fightSide_SetMakibisiCheckFlag(side, 0);
    fightSeq_NextWazaSeq(2);
}

void WS_POKE_RESHUFFLE_WAIT(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    switch (cmd[2]) {
    case 1:
        break;
    case 2:
        fightMsgctrlSetValue(0xd, (GCuptr)fightOutPokemonGetNicknamePtr(p));
        fightMsgctrlSetValue(0xe, (GCuptr)fightOutPokemonGetNamePtr(p));
        break;
    }
    fightSeq_NextWazaSeq(3);
}

void WS_POKE_RESHUFFLE(void) {
    u16 entry;
    void* jump;
    FightOutPokemon* p;
    void* trainer;
    u8* cmd;
    s16 sel;
    u8 fightType;
    fightType = fightFloor_GetFightType(0);
    cmd = (u8*)fightSeq_GetWazaSeq();
    jump = WS_RDPTR(cmd + 2);
    p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    trainer = fightFloorGetFightOutPokemonPtrToFightTrainerPtr(0, p);
    if (fightOutPokemon_GetIrekaetaFlag(p) == 1) {
        fightSeq_NextWazaSeq(6);
        return;
    }
    entry = 0;
    if (fightTrainerSearchIrekaeFightPokemon(trainer, &entry) == 0) {
        fightSeq_JumpWazaSeq(jump);
        return;
    }
    sel = fightTrainerSelectIrekaeFightPokemon(trainer, fightType, 0, p);
    if (sel < 0) {
        if (sel == -2) {
            fightFloorSetTuusinErrorFightResult(0);
        }
        fightSeq_JumpWazaSeq(jump);
        return;
    }
    fightFloor_SetIrekaePokemonPtr(0, p);
    fightOutPokemon_SetIrekaeTargetEntryId(p, sel);
    fightOutPokemon_SetIrekaetaFlag(p, 1);
    fightSeq_NextWazaSeq(6);
}

void WS_RESHUFFLE_CHECK(void) {
    u16 entry;
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    void* trainer = fightFloorGetFightOutPokemonPtrToFightTrainerPtr(0, p);
    if (cmd[6] == 1 && fightFloorCheckFightActionFightOutPokemonIrekaeSelect(0, p, 0) == 1) {
        fightSeq_JumpWazaSeq(WS_RDPTR(cmd + 2));
        return;
    }
    entry = 0;
    if (fightTrainerSearchIrekaeFightPokemon(trainer, &entry) == 0) {
        fightSeq_JumpWazaSeq(WS_RDPTR(cmd + 2));
        return;
    }
    fightSeq_NextWazaSeq(7);
}

void WS_APPEAR_EFFECT_WAIT(void) {
    u8* cmd;
    FightOutPokemon* p;
    void* buff;
    void* trainer;
    u8 fightType;
    u8 mode;
    Pokemon* pokemon;
    u32 encountId;
    fightType = fightFloor_GetFightType(0);
    cmd = (u8*)fightSeq_GetWazaSeq();
    mode = cmd[2];
    p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    fightOutPokemon_GetMotoFightPokemonPtr(p);
    trainer = fightFloorGetFightOutPokemonPtrToFightTrainerPtr(0, p);
    buff = fightTrainer_GetFightoutPokemonBuffPtr(trainer);
    pokemon = fightOutPokemonGetPokemonPtr(p);
    encountId = fightFloor_GetEncountDataId(0);
    if (mode != 2) {
        fightTrainerBallThrowEffect(trainer, p, 2);
    }
    if (!fightOutPokemonIsGcHeroFightOutPokemon(p)) {
        fightMenuFightOutPokemonOpenStatusMenuStart(p, fightType, 0);
        fightMenuFightOutPokemonCloseStatusMenuAnotherMine(0, p, fightType, 0, 1);
    } else {
        fightMenuFightOutPokemonOpenStatusMenuStart(p, fightType, 1);
        fightMenuFightOutPokemonCloseStatusMenuAnotherMine(0, p, fightType, 0, 1);
    }
    if (mode != 2) {
        fightOutPokemonDasuEffect(trainer, p, 1);
        fightOutPokemonDasuEffect(trainer, p, 2);
    }
    fightOutPokemonDasuEffect(trainer, p, 3);
    fightOutPokemonDasuEffect(trainer, p, 4);
    fightMenuFightOutPokemonCloseStatusMenu(p, fightType, 0);
    if (mode != 2) {
        fightTrainerBallThrowEffect(trainer, p, 3);
    }
    fightOutPokemonDasuEffect(trainer, p, 5);
    if (mode == 1 && fightOutPokemonIsJoutaiDataId(p, 0x14) == 1 && fightOutPokemonGetJoutaiMigawariHp(p) > 0) {
        fightMenuCloseMsg();
        fightWazaWzxTypeFuncMigawari(0xa4, p, p, 0, 0);
        for (;;) {
            if (wazaSequenceSysLastWazaIsImpactPoint(4) == 1) {
                break;
            }
            GSthreadSwitch();
        }
        fightOutPokemonFreeAllSequenceWaza(p);
    }
    if (fightFloor_GetFightKindDoZukanMitaFlag(0) == 1 && !fightOutPokemonIsGcHeroFightOutPokemon(p)) {
        fightOutPokemonSetOnZukanFlag(p, 0);
        if ((u16)encountId != 0xd9 && (u16)encountId != 0x15e && pokemonIsDarkPokemon(pokemon) == 1) {
            darkPokemonBiosSetFightFlag(pokemon_GetDarkpokemonDataId(pokemon), 0);
        }
    }
    fightOutPokemonInit(buff);
    fightSeq_NextWazaSeq(3);
}

void WS_APPEAR_EFFECT(void) {
    u8* cmd;
    u8 mode;
    FightOutPokemon* p;
    void* buff;
    void* trainer;
    FightPokemon* moto;
    void* createSeq;
    void* seq;
    s32 irekae;
    cmd = (u8*)fightSeq_GetWazaSeq();
    mode = cmd[2];
    p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    irekae = fightOutPokemon_GetIrekaeTargetEntryId(p);
    moto = fightOutPokemon_GetMotoFightPokemonPtr(p);
    trainer = fightFloorGetFightOutPokemonPtrToFightTrainerPtr(0, p);
    buff = fightTrainer_GetFightoutPokemonBuffPtr(trainer);
    if (fightPokemonIsJoutaiDataId(moto, 4) == 1) {
        fightPokemonSetCountAsJoutaiDataId(moto, 4, 1);
    }
    fightPokemonReplace(fightTrainerGetEntryIdToFightPokemonPtr(trainer, irekae), moto);
    menuFaceLoadFightPokemon(moto, 0, 0);
    createSeq = pokemonCreateSequence(moto);
    fightOutPokemonCreate(p, moto, createSeq);
    fightOutPokemonRegWzxLoad(p);
    fightSeqIrekaeInit(p, buff, mode);
    fightMsgctrlSetValue(0xd, (GCuptr)fightOutPokemonGetNicknamePtr(p));
    seq = fightTrainer_GetSequencePtr(trainer);
    if (seq) {
        void* pseq = fightOutPokemon_GetSequencePtr(p);
        if (pseq) {
            battleGridAddPokemon(seq, pseq);
        }
    }
    if (mode != 2) {
        fightTrainerBallThrowEffect(trainer, p, 0);
    }
    fightOutPokemonDasuEffect(trainer, p, 0);
    battleGridUpdate();
    if (mode != 2) {
        fightTrainerBallThrowEffect(trainer, p, 1);
    }
    fightSeq_NextWazaSeq(3);
}

void WS_APPEAR_EFFECT_INIT(void) {
    u8* cmd = (u8*)fightSeq_GetWazaSeq();
    FightOutPokemon* p = fightTargetGetPtrAsNowFightType(cmd[1], 0);
    fightOutPokemonBiosCopy(fightTrainer_GetFightoutPokemonBuffPtr(fightFloorGetFightOutPokemonPtrToFightTrainerPtr(0, p)), p);
    fightSeq_NextWazaSeq(2);
}

void WS_MODOSU_EFFECT(void) {
    FightOutPokemon* p;
    u8 fightType;
    fightType = fightFloor_GetFightType(0);
    fightSeq_GetWazaSeq();
    p = fightTargetGetPtrAsNowFightType(0x14, 0);
    if (fightOutPokemonCheckFightOut(p) == 1) {
        if (fightOutPokemonIsJoutaiDataId(p, 0x14) == 1 && fightOutPokemonGetJoutaiMigawariHp(p) > 0) {
            fightOutPokemonInitJoutaiDataId(p, 0x14);
            fightOutPokemonFreeAllSequenceWaza(p);
            fightWazaWzxTypeFuncMigawari(0xa4, p, p, 0, 0);
            for (;;) {
                if (wazaSequenceSysLastWazaIsImpactPoint(4) == 1) {
                    break;
                }
                GSthreadSwitch();
            }
        }
        fightOutPokemonFreeAllSequenceWaza(p);
        fightOutPokemonModosuEffect(p, 0);
        fightMenuFightOutPokemonOpenStatusMenu(p, fightType, 1);
        fightMenuFightOutPokemonCloseStatusMenuAnotherMine(0, p, fightType, 0, 1);
        fightOutPokemonModosuEffect(p, 1);
        fightOutPokemonModosuEffect(p, 2);
        fightOutPokemonModosuEffect(p, 3);
        fightOutPokemonModosuEffect(p, 4);
        fightMenuFightOutPokemonCloseStatusMenu(p, fightType, 0);
    }
    fightSeq_NextWazaSeq(1);
}

}
