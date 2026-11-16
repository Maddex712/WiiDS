#include <dolphin/types.h>
#include "game/gc_fused.h"
#include <game/pcret.h>
#include <game/gcdata.h>
#include <game/gcptr.h>
#include <game/statusval.h>
#include <game/menuTables.h>
#include <game/shared/menu_work.h>
#include <game/menuwazastatus.h>

#pragma peephole off

struct Pokemon;
struct FIGHT_TRAINER;
enum MENU_POKEMON_CB_TYPE { MENU_POKEMON_CB_TYPE_0 };

struct MENU_POKEMON_STATUS_TBL {
    u16 id;
    u8 pad2[0x15];
    u8 unk17;
    s16 unk18;
    s16 unk1A;
    u8 pad1C[8];
    u16 unk24;
    u16 unk26;
    u8 unk28;
    u8 unk29;
    u16 unk2A;
    u8 pad2C[4];
};

struct PanelInfo {
    s16 id;
    s16 item;
    s16 x;
    s16 y;
};

struct MenuPokemonWork {
    u8 kind;
    bool canCancel;
    u8 f2;
    u8 f3;
    s8 side;
    s8 prevSel;
    s8 sel;
    s8 f7;
    s8 swapSel;
    u8 pad9[3];
    s32 mode;
    FIGHT_TRAINER* trainer;
    u8 f14;
    u8 f15;
    u16 itemSel;
    u8 listKind;
    u8 f19;
    u8 pad1A[2];
    s32 f1C;
    s32 result;
    u8 f24;
    u8 pad25[3];
};

extern "C" {
extern WAZAOSHIE_DATA* wazaoshie_data;
double sin(double);

static inline f32 sinfInline(f32 x) {
    return (f32)sin(x);
}
u32 menuSubCalcColor(tagWINDOW_WORK*, tagSPRITE_WORK*);
u8* itemDataBiosGetPtr(u16);
u8 itemDataBiosGetKind(u8*);
u32 itemDataBiosGetWazaMachineNo(u8*);
u16 itemDataBiosGetWazaIDByWazaMachineNo(u32);
u8 pokemonIsDarkPokemon(Pokemon*);
u16 pokemonBiosGetPokemonDataId(Pokemon*);
void* pokemonDataBiosGetPtr(u16);
bool pokemonDataBiosGetWazaMcn(void*, PCARG(u8, u16));
u16 pokemonBiosGetPokemonWazaDataId(Pokemon*, PCARG(u16, u32));
u8 pokemonDB_GetTeachWaza(u16, u16);
s32 winSpriteGetLayerID(tagWINDOW_WORK*, tagSPRITE_WORK*);
void winSpriteSetDisp(tagSPRITE_WORK*, bool);
s32 GSmsgPrint2(s32, s32, s32, u32, u32);
s32 GSmsgPrintRect(s32, s32, s32, s16, s16, u32, u32);
u32 GSmsgGetRect(u32);
char* GSmsgGetGSchar(u32);
void windowDrawSprite2(s16, s16, s16, s16, u32, void*, u16, u32, s32);
void menuFaceDispFromID(tagWINDOW_WORK*, tagSPRITE_WORK*, u16);
StatusU32 windowGetParam(tagWINDOW_WORK*, u32);
s32 menuGetCursor(s32);
u8 menuDataBiosGetDispMax(s32);
void menuItemBiosSetXY(s32, u32, s16, s16);
void menuDataBiosSetXY(s32, s16, s16);
f32 timeGetLastFrameSec(void);
u32 windowGetKeyTrg(void);
u16 windowGetKeyRepeat(void);
u8 GSsndPlayNow(u32);
void menuButtonNormal(tagWINDOW_WORK*);
void menuFaceLoad(s32, Pokemon* (*)(void*), void*);
bool menuFaceCheckLoading(void);
f32 pokemonGetDp(Pokemon*);
extern s32 lbl_804E7EB8;
extern s32 lbl_804E7EBC;
void* memset(void*, int, u32);
void GSfloorPush(u32);
u8 GSfloorPop(void);
void floorSetFadeScript(u32, u32);
void floorWaitForChange(void);
void GSthreadSwitch(void);
u8 GSgfxVideoGetVsyncRate(void);
#ifdef __MWERKS__
void menuCreateOffScreen(u8, f32, f32);
#else
void menuCreateOffScreen(f32, f32);
#define menuCreateOffScreen(a, ...) ((void)(a), menuCreateOffScreen(__VA_ARGS__))
#endif
u8 menuGetOffScreenFlag(void);
#ifdef __MWERKS__
void menuReleaseOffScreen(u8, f32);
#else
void menuReleaseOffScreen(f32);
#define menuReleaseOffScreen(a, ...) ((void)(a), menuReleaseOffScreen(__VA_ARGS__))
#endif
void msgctrlSetValue(s32, GCuptr);
u16 pokemonGetSoubiItemDataId(Pokemon*);
s32 heroItemCheckAddItemDataId(void*, PCARG(u16, u32));
s32 heroItemAddItemDataId(void*, PCARG(u16, u32), PCARG(u16, u32), PCARG(s16, u32));
s32 heroItemDecItemDataId(void*, PCARG(u16, u32), PCARG(u16, u32), PCARG(s16, u32));
u16 pokemonDoItemSoubi(Pokemon*, u16, u8);
s32 winMsgOpen(s8, void*, bool, bool);
void winMsgClose(bool);
s8 menuSubOpenYesNo(u8, s16, s16, s32);
s32 menuOpen(s32, s32);
void menuClose(s32);
u16 menuPocket2Call(u32, u32, u32);
u8 menuPocket2GetLastSlotID(void);
void pokemonReplace(Pokemon*, Pokemon*);
void menuSubSleep(f32);
tagWINDOW_WORK* windowSearchID(s32);
void fadeSet(s32, f32);
void fadeCheck(s32);
void pokemonToMenuPokemonStatus(Pokemon*, void*, MENU_POKEMON_STATUS_TBL*, u32);
u16 pokemonGetJoutaiMenuSpriteId(Pokemon*);
u8 pokemonCheckValid(Pokemon*);
Pokemon* heroBiosGetPokemonPtr(void*, u16);
void* fightTrainerGetValidFightPokemonPtr(FIGHT_TRAINER*, u16);
FIGHT_TRAINER* fightFloorGetGcHeroFightTrainerPtr(void*);
StatusU32 fightTrainerGetStatus(FIGHT_TRAINER*, u16, u16, u16);
void* savedataGetStatus(void*, s32);
void* menuCBBios_GetRegisteredHero(void);
u32 GSflagGet(u32);
u8 menuPokemonCheckPokemonChange(s32, s32, s32);
s32 menuGetCursorItemID(s32);
void menuDataBiosGetXY(s32, s16*, s16*);
void winSeqSetMenu(s32, u32);
u8 winSeqCheckMove(s32);
PCRET(u16) menuPokemonGetJoutaiSpriteID(Pokemon*, u8);
s32 menuPokemonStatusOpenFieldMenu(Pokemon*, Pokemon* (*)(Pokemon*, MENU_POKEMON_CB_TYPE, GCsptr), GCsptr);
s32 menuPokemonStatusOpenFightMenu(FIGHT_TRAINER*, Pokemon*, Pokemon* (*)(Pokemon*, MENU_POKEMON_CB_TYPE, GCsptr), GCsptr);
void menuCloseCustom(s32, s32, s32);
#ifdef __MWERKS__
s32 menuOpenCustom(s32, s32, void*, s32, bool, ...);
#else
s32 menuOpenCustom(s32 menu_id, s32 parent_id, s32* cursor_out, u32 close_flags, u8 check_cursor, s32 open_param, ...);
#endif
s32 menuSubOpenSelect2(u8, s32*, s32, s32, s32, s32, s32, u8);
u8 menuOffScreenSetPriority(u8);
void pokemonToMenuWazaStatus(Pokemon*, void*);
StatusU32 pokemonGetStatus(Pokemon*, u16, u16, u16);
StatusU32 wazaGetStatus(void*, u16, u16, u32);
void menuPokemonUpdate(void);
void menuPokemonClose(void);
}

static MenuPokemonWork _menuPokemon;
static MENU_POKEMON_STATUS_TBL _menuPokemonStatusAll[6];

extern "C" GC_CONST u16 spriteTbl[14];
extern "C" {
GC_CONST u16 spriteTbl[14] ;
}

static PanelInfo menu_info[2][6] ;

static Pokemon* _menuPokemonStatusCB(Pokemon*, MENU_POKEMON_CB_TYPE, GCsptr);
static Pokemon* _menuPokemonGetPokemonPtr(u16);
static void* _menuPokemonGetHeroPtr(void);
static s32 _menuPokemonSub(u8, u32, FIGHT_TRAINER*);
static s8 _menuPokemonActionStatus(s8, bool);
static s8 _menuPokemonOpenList(bool);
static u8 _menuPokemonMovePanel(s32, u8);
static s32 _menuPokemonActionFieldItem(s8);
static void _menuPokemonMakeStatus(MENU_POKEMON_STATUS_TBL*);
static s32 _menuPokemonActionGive(s8, s16, u16, u16*);
static s32 _menuPokemonOpen(u8, u8, u8, u16, FIGHT_TRAINER*, bool);
static s16 _menuPokemonTimer;
static void* _menuPokemonGetHeroPtr(void) {
    FIGHT_TRAINER* t = _menuPokemon.trainer;
    switch (_menuPokemon.mode) {
    case 0:
        if (GSflagGet(0x30D) == 0) {
            return savedataGetStatus(0, 2);
        }
        return menuCBBios_GetRegisteredHero();
    case 1:
        if (t == 0) {
            fightFloorGetGcHeroFightTrainerPtr(0);
            return 0;
        }
        return (void*)fightTrainerGetStatus(t, 0, 0x44, 0);
    default:
        return 0;
    }
}

static Pokemon* _menuPokemonGetPokemonPtr(u16 index) {
    Pokemon* p = 0;
    FIGHT_TRAINER* t = _menuPokemon.trainer;
    void* fp;
    switch (_menuPokemon.mode) {
    case 0: {
        void* hero;
        if (index >= 6) {
            return 0;
        }
        hero = _menuPokemonGetHeroPtr();
        if (hero == 0) {
            return 0;
        }
        p = heroBiosGetPokemonPtr(hero, index);
        break;
    }
    case 1:
        if (index >= 6) {
            return 0;
        }
        if (t == 0) {
            fightFloorGetGcHeroFightTrainerPtr(0);
            return 0;
        }
        fp = fightTrainerGetValidFightPokemonPtr(t, index);
        if (fp == 0) {
            return 0;
        }
        p = (Pokemon*)pokemonGetStatus((Pokemon*)fp, 0, 0xCC, 0);
        break;
    case 2:
        if (index >= 0x1E) {
            return 0;
        }
        p = 0;
        break;
    }
    if (pokemonCheckValid(p) == 0) {
        return 0;
    }
    return p;
}

extern "C" {
static u16 xddt_local2_status_joutai_tbl [ 7 ];
PCRET(u16) menuPokemonGetJoutaiSpriteID(Pokemon* pokemon, u8 status) {
    static u16 top_joutai_tbl[8] = {0, 0x142, 0x146, 0x143, 0x144, 0x145, 0x148, 0};
    ;
    u16 idx;
    if ((bool)pokemonGetStatus(pokemon, 0, 0x7B, 0) == 1) {
        idx = 1;
    } else {
        switch (pokemonGetJoutaiMenuSpriteId(pokemon)) {
        case 0x83:
            idx = 2;
            break;
        case 0x80:
            idx = 3;
            break;
        case 0x81:
            idx = 4;
            break;
        case 0x82:
            idx = 5;
            break;
        case 0x85:
            idx = 6;
            break;
        default:
            idx = 0;
            break;
        }
    }
    if (status == 0) {
        return top_joutai_tbl[idx];
    }
    return xddt_local2_status_joutai_tbl[idx];
}
}

extern "C" {
void menuPokemonUpdate(void) {
    _menuPokemonMakeStatus(_menuPokemonStatusAll);
}
}

static void _menuPokemonMakeStatus(MENU_POKEMON_STATUS_TBL* tbl) {
    MENU_POKEMON_STATUS_TBL* e;
    Pokemon* p;
    u16 i;
    memset(tbl, 0, 0x120);
    for (i = 0; i < 6; i++) {
        e = &tbl[i];
        p = _menuPokemonGetPokemonPtr(i);
        if (p == 0) {
            e->id = 0;
        } else {
            pokemonToMenuPokemonStatus(p, 0, e, -1);
            bool flag = pokemonGetStatus(p, 0, 0x7B, 0) != 0;
            if (flag == true) {
                e->unk1A = 0;
            }
            e->unk24 = menuPokemonGetJoutaiSpriteID(p, 0);
        }
    }
}

static s8 _menuPokemonOpenList(bool flag) {
    s32 sel = _menuPokemon.sel;
    s8 r = menuOpenCustom(0x4C, 0, &sel, 0, flag, 1, _menuPokemonStatusAll);
    if (!flag) {
        tagWINDOW_WORK* w = windowSearchID(0x4C);
        if (w != 0) {
            w->done = 1;
        }
        r = sel;
    }
    if (_menuPokemon.canCancel == 0) {
        r = -2;
    }
    return r;
}

static s8 _menuPokemonActionStatus(s8 index, bool field) {
    Pokemon* pokemon;
    menuCloseCustom(0x4C, 0, 1);
    pokemon = _menuPokemonGetPokemonPtr(index);
    fadeSet(3, 0.25f);
    fadeCheck(1);
    menuOffScreenSetPriority(0);
    if (field) {
        menuPokemonStatusOpenFieldMenu(pokemon, _menuPokemonStatusCB, 0);
    } else {
        menuPokemonStatusOpenFightMenu(_menuPokemon.trainer, pokemon, _menuPokemonStatusCB, 0);
    }
    menuOffScreenSetPriority(1);
    _menuPokemonOpenList(false);
    fadeSet(2, 0.25f);
    fadeCheck(1);
    return _menuPokemon.sel;
}

static s32 _menuPokemonActionFieldItem(s8 index) {
    u16 out;
    s32 r;
    _menuPokemon.listKind = 3;
    r = menuOpen(0x5D, 1);
    menuClose(0x5D);
    switch (r) {
    case 0: {
        u16 item;
        u8 slot;
        menuClose(0x4C);
        item = menuPocket2Call(2, 0, 0);
        slot = menuPocket2GetLastSlotID();
        _menuPokemonOpenList(false);
        if (item == 0) {
            return -1;
        }
        _menuPokemonActionGive(index, slot, item, &out);
        r = 0;
        break;
    }
    case 1:
        _menuPokemonActionGive(index, -1, 0, &out);
        r = 0;
        break;
    case 2:
    case -1:
        r = -1;
        break;
    }
    return r;
}

static u8 _menuPokemonMovePanel(s32 index, u8 mode) {
    s16 x;
    s16 y;
    s16 id = menu_info[(s8)_menuPokemon.side][index].id;
    menuDataBiosGetXY(id, &x, &y);
    switch (mode) {
    case 0:
        winSeqSetMenu(id, x > 0xFA ? 0x98 : 0xA0);
        break;
    case 1:
        winSeqSetMenu(id, x > 0xFA ? 0x9C : 0xA4);
        break;
    case 2:
        return winSeqCheckMove(id);
    }
    return 1;
}

static s32 _menuPokemonActionField(s8 index) {
    s32 r;
    do {
        _menuPokemon.listKind = 1;
        if (GSflagGet(0x30D) == 0) {
            r = menuOpen(0x4F, 1);
            menuClose(0x4F);
        } else {
            r = menuOpen(0x5A, 1);
            menuClose(0x5A);
        }
        switch (r) {
        case 0x1AD:
        case 0x16F:
            index = _menuPokemonActionStatus(index, true);
            r = 0;
            break;
        case 0x1AF:
        case 0x171: {
            int other;
            s32 j;
            Pokemon* a;
            Pokemon* b;
            _menuPokemon.swapSel = index;
            _menuPokemon.listKind = 2;
            other = _menuPokemonOpenList(true);
            _menuPokemon.swapSel = -1;
            if (menuGetCursorItemID(0x4C) != 0x163 && other != -1) {
                a = _menuPokemonGetPokemonPtr(index);
                b = _menuPokemonGetPokemonPtr((u16)other);
                if (a != 0 && a != 0) {
                    r = index;
                    j = other;
                    _menuPokemonMovePanel(r, 1);
                    _menuPokemonMovePanel(j, 1);
                    while (_menuPokemonMovePanel((s32)r, 2) != 0) {
                        GSthreadSwitch();
                    }
                    pokemonReplace(a, b);
                    menuPokemonUpdate();
                    menuSubSleep(0.25f);
                    _menuPokemonMovePanel(r, 0);
                    _menuPokemonMovePanel(j, 0);
                    while (_menuPokemonMovePanel(r, 2) != 0) {
                        GSthreadSwitch();
                    }
                }
            }
            r = -1;
            break;
        }
        case 0x173:
            if (_menuPokemonActionFieldItem(index) == -1) {
                r = 0;
            } else {
                r = -1;
            }
            break;
        case 0x1B1:
        case 0x175:
        case -1:
            r = -1;
            break;
        }
    } while (r != -1);
    return 0;
}

static s32 _menuPokemonActionFight(s8 index) {
    s32 r;
    _menuPokemon.listKind = 1;
    while (1) {
        if (_menuPokemon.f19 != 0) {
            r = menuOpen(0x50, 1);
            menuClose(0x50);
        } else {
            r = menuOpen(0x51, 1);
            menuClose(0x51);
        }
        switch (r) {
        case 0:
            if (menuPokemonCheckPokemonChange(_menuPokemon.f1C, (GCsptr)_menuPokemon.trainer, index) != 0) {
                r = -1;
            } else {
                r = 1;
            }
            break;
        case 1:
            index = _menuPokemonActionStatus(index, false);
            continue;
        case 2:
        case -1:
            r = 1;
            break;
        }
        break;
    }
    return r;
}

static s32 _menuPokemonActionGive(s8 index, s16 slot, u16 item, u16* outItem) {
    int yes;
    u16 have;
    Pokemon* pokemon;
    void* hero;
    pokemon = _menuPokemonGetPokemonPtr(index);
    msgctrlSetValue(0x32, (GCuptr)&_menuPokemonStatusAll[(s8)_menuPokemon.sel]);
    have = pokemonGetSoubiItemDataId(pokemon);
    hero = _menuPokemonGetHeroPtr();
    if (hero == 0) {
        return -1;
    }
    if (item == 0) {
        if (have != 0) {
            if (heroItemCheckAddItemDataId(hero, have) <= 0) {
                winMsgOpen(2, (void*)0x3ACF, 1, 1);
            } else {
                u16 old;
                heroItemAddItemDataId(hero, have, 1, -1);
                old = pokemonDoItemSoubi(pokemon, 0, 0);
                if (outItem != 0) {
                    *outItem = old;
                }
                msgctrlSetValue(0x2D, old);
                winMsgOpen(2, (void*)0x3ACD, 1, 1);
            }
        } else {
            winMsgOpen(2, (void*)0x3ACE, 1, 1);
        }
    } else if (have != 0) {
        msgctrlSetValue(0x2D, have);
        winMsgOpen(2, (void*)0x3ACA, 1, 0);
        yes = menuSubOpenYesNo(0, -1, -1, 0);
        winMsgClose(1);
        if (yes != 0) {
            return -1;
        }
        if (heroItemDecItemDataId(hero, item, 1, slot) != 0) {
            winMsgOpen(2, (void*)0x3ACF, 1, 1);
        } else if (heroItemCheckAddItemDataId(hero, have) <= 0) {
            heroItemAddItemDataId(hero, item, 1, -1);
            winMsgOpen(2, (void*)0x3ACF, 1, 1);
        } else {
            u16 old;
            heroItemAddItemDataId(hero, have, 1, -1);
            old = pokemonDoItemSoubi(pokemon, 0, 0);
            if (outItem != 0) {
                *outItem = old;
            }
            pokemonDoItemSoubi(pokemon, item, 1);
            msgctrlSetValue(0x2D, old);
            msgctrlSetValue(0x2E, item);
            winMsgOpen(2, (void*)0x3ACB, 1, 0);
        }
    } else if (heroItemDecItemDataId(hero, item, 1, slot) == 0) {
        pokemonDoItemSoubi(pokemon, item, 1);
        msgctrlSetValue(0x2D, item);
        winMsgOpen(2, (void*)0x3ACC, 1, 0);
    }
    winMsgClose(1);
    menuPokemonUpdate();
    return -1;
}

static Pokemon* _menuPokemonCbLoadPokemonList(void* p) {
    u16* idx = (u16*)p;
    Pokemon* pokemon = _menuPokemonGetPokemonPtr(*idx);
    (*idx)++;
    return pokemon;
}

extern "C" {
s32 menuPokemonMain(void) {
    menuOffScreenSetPriority(0);
    _menuPokemonSub(_menuPokemon.kind, _menuPokemon.itemSel, _menuPokemon.trainer);
    menuPokemonClose();
    menuOffScreenSetPriority(1);
    return 0;
}
}

extern "C" {
s32 menuPokemonOpen(u8 kind, u16 sel, FIGHT_TRAINER* trainer) {
    _menuPokemon.side = 1;
    return _menuPokemonOpen(kind, 0, 0, sel, trainer, true);
}
}

extern "C" {
s32 menuPokemonOpenFight(u8 side, u8 sel, FIGHT_TRAINER* trainer, s32 f1c) {
    if (side == 1) {
        _menuPokemon.side = 0;
    } else {
        _menuPokemon.side = 1;
    }
    _menuPokemon.f1C = f1c;
    return _menuPokemonOpen(2, 0, 0, sel, trainer, true);
}
}

extern "C" {
s32 menuPokemonOpenItemUse(u8 kind, bool flag, u16 sel, FIGHT_TRAINER* trainer) {
    _menuPokemon.side = 1;
    return _menuPokemonOpen(kind, 0, 0, sel, trainer, flag);
}
}

extern "C" {
s32 menuPokemonOpenItemGive(u8 a, u8 b, u16 sel, FIGHT_TRAINER* trainer) {
    _menuPokemon.side = 1;
    return _menuPokemonOpen(5, a, b, sel, trainer, true);
}
}

static s32 _menuPokemonOpen(u8 kind, u8 a, u8 b, u16 sel, FIGHT_TRAINER* trainer, bool canCancel) {
    _menuPokemon.kind = kind;
    _menuPokemon.f14 = a;
    _menuPokemon.f15 = b;
    _menuPokemon.itemSel = sel;
    _menuPokemon.trainer = trainer;
    _menuPokemon.canCancel = canCancel;
    _menuPokemon.result = 0;
    _menuPokemon.f19 = 1;
    _menuPokemon.f24 = 0;
    switch (kind) {
    case 2:
    case 4:
        _menuPokemon.mode = 1;
        break;
    case 3:
    default:
        _menuPokemon.mode = 0;
        break;
    }
    switch (_menuPokemon.kind) {
    case 3:
    case 4:
    case 5:
        menuOffScreenSetPriority(0);
        _menuPokemon.f2 = 0;
        _menuPokemon.f3 = 0;
        break;
    case 1:
        _menuPokemon.f2 = 1;
        _menuPokemon.f3 = 0;
        break;
    case 2:
        _menuPokemon.f2 = 1;
        _menuPokemon.f3 = 1;
        if (sel == 0) {
            _menuPokemon.f19 = 0;
        }
    case 6:
    case 7:
    case 8:
    default: {
        u32 rate = GSgfxVideoGetVsyncRate();
        f32 duration = rate;
        duration = 20.0f / duration;
        menuCreateOffScreen(rate, duration, 50.0f);
        while (menuGetOffScreenFlag() == 0) {
            GSthreadSwitch();
        }
        _menuPokemon.f2 = 1;
        _menuPokemon.f3 = 2;
        break;
    }
    }
    if (_menuPokemon.f2 == 1) {
        GSfloorPush(0x3A5);
        if (_menuPokemon.f3 != 1) {
            floorSetFadeScript(0, 0);
        }
        floorWaitForChange();
        if (_menuPokemon.f3 == 2) {
            u32 rate = GSgfxVideoGetVsyncRate();
            f32 duration = rate;
            duration = 20.0f / duration;
            menuReleaseOffScreen(rate, duration);
        }
    } else {
        _menuPokemonSub(kind, sel, trainer);
    }
    switch (_menuPokemon.kind) {
    case 3:
    case 4:
    case 5:
        menuOffScreenSetPriority(1);
        break;
    }
    _menuPokemon.side = 1;
    return _menuPokemon.result;
}

static s32 _menuPokemonSub(u8 kind, u32 sel, FIGHT_TRAINER* trainer) {
    u16 n;
    s32 r;
    s8 chosen;
    _menuPokemon.sel = _menuPokemon.f7;
    if ((s8)_menuPokemon.f7 < 0) {
        _menuPokemon.sel = 0;
    }
    _menuPokemon.swapSel = -1;
    if (_menuPokemon.canCancel == 0) {
        _menuPokemon.sel = -1;
    }
    menuPokemonUpdate();
    static s16 pokemonFaceLoadCnt;
    pokemonFaceLoadCnt = 0;
    menuFaceLoad(0, _menuPokemonCbLoadPokemonList, &pokemonFaceLoadCnt);
    n = 0;
    while (n < 6) {
        if (_menuPokemonGetPokemonPtr(n) == 0) {
            break;
        }
        n++;
    }
    if (n <= (s8)_menuPokemon.sel) {
        _menuPokemon.sel = n - 1;
    }
    while (1) {
        _menuPokemon.listKind = 0;
        chosen = _menuPokemonOpenList(true);
        _menuPokemon.sel = chosen;
        if (chosen == -2) {
            break;
        }
        if (menuGetCursorItemID(0x4C) == 0x163 || chosen == -1) {
            _menuPokemon.sel = -1;
            break;
        }
        switch (_menuPokemon.kind) {
        case 0:
            break;
        case 1:
            r = _menuPokemonActionField(chosen);
            break;
        case 2:
            r = _menuPokemonActionFight(chosen);
            break;
        case 3:
        case 4:
        case 8:
            r = -1;
            break;
        case 5: {
            u16 out;
            r = _menuPokemonActionGive(chosen, _menuPokemon.f15, _menuPokemon.itemSel, &out);
            break;
        }
        case 6:
            r = -1;
            break;
        case 7: {
            Pokemon* p = _menuPokemonGetPokemonPtr(chosen);
            if (p == 0) {
                continue;
            }
            if (pokemonIsDarkPokemon(p) == 0) {
                winMsgOpen(2, (void*)0xC54C, 1, 0);
                winMsgClose(1);
                continue;
            }
            if (pokemonGetDp(p) != 0.0f) {
                winMsgOpen(2, (void*)0xC54D, 1, 0);
                winMsgClose(1);
                continue;
            }
            r = -1;
            break;
        }
        }
        if (r < 0) {
            break;
        }
    }
    while (menuFaceCheckLoading()) {
        GSthreadSwitch();
    }
    _menuPokemon.result = (s8)_menuPokemon.sel;
    return r;
}

extern "C" {
void menuPokemonClose(void) {
    menuCloseCustom(0x4C, 0, 1);
    if (_menuPokemon.f2 != 0) {
        menuOffScreenSetPriority(1);
        GSfloorPop();
        if (_menuPokemon.f3 != 1) {
            floorSetFadeScript(0, 0);
        }
        floorWaitForChange();
    }
}
}

extern "C" {
s32 menuPokemonCtrl(tagWINDOW_WORK* w) {
    MENU_POKEMON_STATUS_TBL* tbl = (MENU_POKEMON_STATUS_TBL*)windowGetParam(w, 0);
    int i;
    if (tbl == 0) {
        return 0;
    }
    menuOffScreenSetPriority(0);
    if (w->state == 0) {
        w->prevCursor = -1;
        if (w->seqSet == 0) {
            for (i = 0; i < 6; i++) {
                menuItemBiosSetXY(w->menuID, menu_info[(s8)_menuPokemon.side][i].item,
                                  menu_info[(s8)_menuPokemon.side][i].x, menu_info[(s8)_menuPokemon.side][i].y);
                menuDataBiosSetXY(menu_info[(s8)_menuPokemon.side][i].id, menu_info[(s8)_menuPokemon.side][i].x,
                                  menu_info[(s8)_menuPokemon.side][i].y);
                menuOpenCustom(menu_info[(s8)_menuPokemon.side][i].id, 0x4C, 0, 0, 0, 2, &tbl[i], i);
                _menuPokemonMovePanel(i, 0);
            }
            winSeqSetMenu(w->menuID, 1);
            w->seqSet = 1;
        }
        menuOffScreenSetPriority(0);
    } else if (w->state == 3 && w->seqSet == 0) {
        for (i = 0; i < 6; i++) {
            _menuPokemonMovePanel(i, 1);
        }
        winSeqSetMenu(w->menuID, 7);
        w->seqSet = 1;
    }
    _menuPokemonTimer = (s32)(GC_FUSED(60.0f, timeGetLastFrameSec()) + (f32)_menuPokemonTimer) % 1000;
    return 0;
}
}

extern "C" {
void menuPokemonCursor(tagWINDOW_WORK* w) {
    s8 cur = (s8)w->cursor;
    u8 valid[8];
    s8 dir = 0;
    s32 i;
    u16 key;
    for (i = 0; i < 6; i++) {
        if (_menuPokemonGetPokemonPtr(i) != 0) {
            valid[i] = 1;
        } else {
            valid[i] = 0;
        }
    }
    valid[i] = 1;
    key = windowGetKeyRepeat();
    if(key&1){if(cur==6)cur=4;else if(cur>=2)cur-=2;}
    else if(key&2){if(cur<4)cur+=2;else cur=6;}
    else if(key&4){if(cur<6&&(cur&1))cur--;}
    else if(key&8){if(cur<6&&!(cur&1))cur++;}
    if(cur<6&&!valid[cur])cur=(s8)w->cursor;
    if (w->cursor != cur) {
        _menuPokemon.prevSel = w->cursor;
        _menuPokemon.sel = cur;
        _menuPokemon.f7 = cur;
        w->cursor = cur;
    }
}
}

extern "C" {
void menuPokemonButton(tagWINDOW_WORK* w) {
    u8 pressed = 0;
    if (_menuPokemon.canCancel == 0) {
        w->done = 1;
        return;
    }
    if (winSeqCheckMove(0x4B) == 0) {
        u16 trg = windowGetKeyTrg();
        if (trg & 0x10) {
            if ((u16)menuGetCursorItemID(w->menuID) == 0x162) {
                pressed = 1;
            }
        } else if (trg & 0x20) {
            pressed = 1;
        }
        if (pressed != 0) {
            if (_menuPokemon.f19 == 0) {
                GSsndPlayNow(0x13);
                return;
            }
            w->done = 1;
            w->cancel = 1;
        } else {
            menuButtonNormal(w);
        }
    }
}
}

extern "C" {
void menuPokemonDrawHelp(tagWINDOW_WORK* w, tagSPRITE_WORK* sp) {
    u32 msg = 0;
    switch (_menuPokemon.listKind) {
    case 0:
        switch (_menuPokemon.kind) {
        case 8:
            msg = 0x3AC7;
            break;
        case 3:
        case 4:
            if (itemDataBiosGetKind(itemDataBiosGetPtr(_menuPokemon.itemSel)) == 4) {
                msg = 0x3AC7;
            } else if (_menuPokemon.f24 != 0) {
                msg = 0x3C3A;
            } else {
                msg = 0x3AC5;
            }
            break;
        case 5:
            msg = 0x3AC6;
            break;
        case 1:
        case 2:
        default:
            msg = 0x3AC1;
            break;
        }
        break;
    case 1:
        msgctrlSetValue(0x32, (GCuptr)&_menuPokemonStatusAll[(s8)_menuPokemon.sel]);
        msg = 0x3AC2;
        break;
    case 2:
        msg = 0x3AC3;
        break;
    case 3:
        msg = 0x3AC4;
        break;
    }
    if (msg != 0) {
        GSmsgPrint2(winSpriteGetLayerID(w, sp), 0, 0, w->alpha | ~0xFF, msg);
    }
}
}

static bool _menuPokemonCheckItem(tagSPRITE_WORK* sp, s8 side) {
    bool enable = 1;
    s32 color = -1;
    if (side == -1) {
        switch (sp->id) {
        case 0x131:
        case 0x133:
        case 0x135:
        case 0x136:
        case 0x137:
        case 0x138:
        case 0x139:
        case 0x13A:
        case 0x13B:
        case 0x13E:
        case 0x140:
        case 0x141:
        case 0x142:
        case 0x143:
        case 0x144:
        case 0x145:
        case 0x146:
        case 0x2DD:
            enable = 0;
            break;
        }
    } else if (side == 1) {
        switch (sp->id) {
        case 0x131:
        case 0x132:
        case 0x13C:
        case 0x13D:
        case 0x1C4:
        case 0x2CF:
        case 0x2D0:
        case 0x2DD:
            color = 0x808080FF;
            break;
        }
    }
    winSpriteSetDisp(sp, enable);
    GC_SETBYTES32(&sp->r, color);
    return enable;
}

extern "C" {
void menuPokemonDrawCur(tagWINDOW_WORK* w, tagSPRITE_WORK* sp) {
    winSpriteSetDisp(sp, false);
    switch (menuGetCursor(w->menuID)) {
    case 0:
        switch (sp->id) {
        case 0x16E:
        case 0x191:
        case 0x1A8:
        case 0x1AC:
        case 0x1BB:
            winSpriteSetDisp(sp, true);
            break;
        }
        break;
    case 1:
        switch (sp->id) {
        case 0x170:
        case 0x183:
        case 0x193:
        case 0x1AE:
        case 0x1BD:
            winSpriteSetDisp(sp, true);
            break;
        }
        break;
    case 2:
        switch (sp->id) {
        case 0x172:
        case 0x195:
        case 0x1AA:
        case 0x1B0:
        case 0x1BF:
            winSpriteSetDisp(sp, true);
            break;
        }
        break;
    case 3:
        switch (sp->id) {
        case 0x174:
            winSpriteSetDisp(sp, true);
            break;
        }
        break;
    }
}
}

extern "C" {
void menuPokemonDrawItem(tagWINDOW_WORK* w, tagSPRITE_WORK* sp) {
    u16 tex;
    Pokemon* pokemon;
    u32 sexMsg;
    MENU_POKEMON_STATUS_TBL* info;
    u32 color;
    GCsptr item;
    s16 i;
    s16 width;
    s16 x;
    u32 msg;
    u16 idx;
    s8 side;
    info = (MENU_POKEMON_STATUS_TBL*)windowGetParam(w, 0);
    sexMsg = 0;
    side = 0;
    if (info == 0) {
        return;
    }
    if (info->id == 0) {
        side = -1;
    } else if (info->unk1A == 0) {
        side = 1;
    }
    if (_menuPokemonCheckItem(sp, side) == 0) {
        return;
    }
    color = menuSubCalcColor(w, sp);
    idx = windowGetParam(w, 1);
    if (_menuPokemon.kind == 3 || _menuPokemon.kind == 4) {
        item = (GCsptr)itemDataBiosGetPtr(_menuPokemon.itemSel);
        if (itemDataBiosGetKind((u8*)item) == 4) {
            switch (sp->id) {
            case 0x137:
            case 0x142: {
                u32 machine;
                u16 waza;
                Pokemon* pokemon;
                machine = itemDataBiosGetWazaMachineNo((u8*)item);
                pokemon = _menuPokemonGetPokemonPtr(idx);
                if (pokemonIsDarkPokemon(pokemon)) {
                    msg = 0x3AC9;
                } else if (!pokemonDataBiosGetWazaMcn(pokemonDataBiosGetPtr(pokemonBiosGetPokemonDataId(pokemon)), machine)) {
                    msg = 0x3AC9;
                } else {
                    waza = itemDataBiosGetWazaIDByWazaMachineNo(machine);
                    for (i = 0; i < 4; i++) {
                        if (waza == pokemonBiosGetPokemonWazaDataId(pokemon, i)) {
                            break;
                        }
                    }
                    if (i < 4) {
                        msg = 0x3C3C;
                    } else {
                        msg = 0x3AC8;
                    }
                }
                GSmsgPrint2(winSpriteGetLayerID(w, sp), 0, -4, color, msg);
            }
            case 0x138:
            case 0x143:
                winSpriteSetDisp(sp, false);
                return;
            }
        }
    } else if (_menuPokemon.kind == 8) {
        switch (sp->id) {
        case 0x137:
        case 0x142: {
            u16 waza;
            s16 i;
            pokemon = _menuPokemonGetPokemonPtr(idx);
            if (pokemonIsDarkPokemon(pokemon)) {
                msg = 0x3AC9;
            } else if (!pokemonDB_GetTeachWaza(pokemonBiosGetPokemonDataId(pokemon), _menuPokemon.itemSel)) {
                msg = 0x3AC9;
            } else {
                u8 tbl[12] = {7, 10, 2, 0, 5, 1, 4, 3, 6, 9, 11, 8};
                waza = wazaoshie_data[tbl[_menuPokemon.itemSel]].id;
                for (i = 0; i < 4; i++) {
                    if (waza == pokemonBiosGetPokemonWazaDataId(pokemon, i)) {
                        break;
                    }
                }
                if (i < 4) {
                    msg = 0x3C3C;
                } else {
                    msg = 0x3AC8;
                }
            }
            GSmsgPrint2(winSpriteGetLayerID(w, sp), 0, -4, color, msg);
        }
        case 0x138:
        case 0x143:
            winSpriteSetDisp(sp, false);
            return;
        }
    }
    switch (sp->id) {
    case 0x132:
    case 0x13C:
        if (info->unk29 == 0) {
            winSpriteSetDisp(sp, true);
        } else {
            winSpriteSetDisp(sp, false);
        }
        break;
    case 0x13D:
    case 0x1C4:
        if (info->unk29 == 1) {
            winSpriteSetDisp(sp, true);
        } else {
            winSpriteSetDisp(sp, false);
        }
        break;
    case 0x2CF:
    case 0x2D0:
        if (info->unk29 == 2) {
            winSpriteSetDisp(sp, true);
        } else {
            winSpriteSetDisp(sp, false);
        }
        break;
    case 0x13B:
    case 0x146:
        if (info->unk24 != 0) {
            windowDrawSprite2(0, 0, sp->w, sp->h, color, w, info->unk24, 0, winSpriteGetLayerID(w, sp));
        }
        break;
    case 0x13A:
    case 0x145:
        if (info->unk2A != 0) {
            winSpriteSetDisp(sp, true);
        } else {
            winSpriteSetDisp(sp, false);
        }
        break;
    case 0x136:
    case 0x141:
        GSmsgPrint2(winSpriteGetLayerID(w, sp), 0x2C, 0, color, 0x3AE8);
        msgctrlSetValue(0x5F, info->unk1A);
        GSmsgPrintRect(winSpriteGetLayerID(w, sp), 0, 0, 0x2A, sp->h, color, 0x4357);
        msgctrlSetValue(0x5F, info->unk18);
        GSmsgPrintRect(winSpriteGetLayerID(w, sp), 0, 0, sp->w - 2, sp->h, color, 0x4357);
        break;
    case 0x135:
    case 0x140:
        msgctrlSetValue(0x5F, info->unk17);
        GSmsgPrintRect(winSpriteGetLayerID(w, sp), 0, 0, sp->w, sp->h, color, 0x4351);
        break;
    case 0x133:
    case 0x13E: {
        msgctrlSetValue(0x37, (GCuptr)info);
        GSmsgPrint2(winSpriteGetLayerID(w, sp), 0, 0, color, 0x4277);
        x = (GSmsgGetRect(0x4277) >> 16) + 4;
        switch (info->unk28) {
        case 0:
            sexMsg = 0x9C5;
            break;
        case 1:
            sexMsg = 0x9C6;
            break;
        case 2:
            sexMsg = 0;
            break;
        }
        if (sexMsg != 0) {
            msgctrlSetValue(0x37, (GCuptr)GSmsgGetGSchar(sexMsg));
            GSmsgPrint2(winSpriteGetLayerID(w, sp), x - 2, 0, color, 0x4276);
        }
        break;
    }
    case 0x131:
    case 0x2DD:
        menuFaceDispFromID(w, sp, info->unk26);
        break;
    case 0x138:
    case 0x143: {
        s16 cur = info->unk1A;
        s16 max = info->unk18;
        if (cur <= 0) {
            tex = 0;
        } else if (cur <= max * 20 / 100) {
            tex = 0xF5;
        } else if (cur <= max * 50 / 100) {
            tex = 0xF4;
        } else {
            tex = 0xF3;
        }
        width = (max - 1 + cur * sp->w) / max;
        if (tex != 0) {
            windowDrawSprite2(0, 0, width, sp->h, color, w, tex, 0, winSpriteGetLayerID(w, sp));
        }
        break;
    }
    case 0x160:
    case 0x161: {
        s32 id;
        if ((s8)_menuPokemon.swapSel >= 0 && (s8)_menuPokemon.swapSel < 6) {
            id = menu_info[(s8)_menuPokemon.side][(s8)_menuPokemon.swapSel].id;
        } else {
            id = 0;
        }
        if (id == w->menuID) {
            s32 t;
            t = _menuPokemonTimer % 60;
            *(u32*)&sp->r = 0xFFFF00FF;
            if (t < 30) {
                f32 ang = 3.14f * ((f32)t / 30.0f);
                sp->alpha -= (u8)(128.0f * sinfInline(ang));
            }
            winSpriteSetDisp(sp, true);
        } else {
            winSpriteSetDisp(sp, false);
        }
        break;
    }
    case 0x156:
    case 0x157: {
        s32 id;
        if ((s8)_menuPokemon.sel >= 0 && (s8)_menuPokemon.sel < 6) {
            id = menu_info[(s8)_menuPokemon.side][(s8)_menuPokemon.sel].id;
        } else {
            id = 0;
        }
        if (id == w->menuID) {
            s32 t;
            t = _menuPokemonTimer % 60;
            *(u32*)&sp->r = 0xFFFFFFFF;
            if (t < 30) {
                f32 ang = 3.14f * ((f32)t / 30.0f);
                sp->alpha -= (u8)(128.0f * sinfInline(ang));
            }
            winSpriteSetDisp(sp, true);
        } else {
            winSpriteSetDisp(sp, false);
        }
        break;
    }
    case 0x162:
        if ((s8)_menuPokemon.sel == 6) {
            winSpriteSetDisp(sp, true);
        } else {
            winSpriteSetDisp(sp, false);
        }
        break;
    case 0x163:
    case 0x164:
        switch (_menuPokemon.listKind) {
        case 0:
        case 2:
            winSpriteSetDisp(sp, true);
            if (sp->id == 0x164) {
                u32 text;
                s32 tw;
                if (_menuPokemon.kind == 7 || _menuPokemon.kind == 6) {
                    text = 0x3BB4;
                } else {
                    text = 0x3C3E;
                }
                tw = GSmsgGetRect(text) >> 16;
                GSmsgPrint2(winSpriteGetLayerID(w, sp), (sp->w - tw) / 2, 0, w->alpha | ~0xFF, text);
            }
            break;
        case 1:
        case 3:
            winSpriteSetDisp(sp, false);
            break;
        }
        break;
    }
}
}

static Pokemon* _menuPokemonStatusCB(Pokemon* pokemon, MENU_POKEMON_CB_TYPE type, GCsptr) {
    s8 next;
    s8 cur = _menuPokemon.sel;
    switch ((s32)type) {
    case 1:
        next = cur;
        next -= 1;
        break;
    case 2:
        next = 1;
        next += cur;
        break;
    default:
        return pokemon;
    }
    if (next >= 6) {
        next = 5;
    }
    if (next < 0) {
        next = 0;
    }
    if (next != cur) {
        Pokemon* p = _menuPokemonGetPokemonPtr(next);
        if (p != 0) {
            pokemon = p;
            _menuPokemon.sel = next;
        }
    }
    return pokemon;
}

extern "C" {
s32 menuPokemonOpenWazaLight(s8 index) {
    s32* p;
    s32 r;
    s32 n;
    Pokemon* pokemon = _menuPokemonGetPokemonPtr(index);
    s32 i;
    s32 ids[4];
    static u8 wazaStatus[MENU_WAZA_STATUS_SIZE];
    if (pokemon == 0) {
        return -1;
    }
    pokemonToMenuWazaStatus(pokemon, wazaStatus);
    for (p = &ids[i = n = 0]; i < 4; p++, i++) {
        u16 w = pokemonGetStatus(pokemon, 0, 0x7F, i);
        if (w != 0) {
            *p = wazaGetStatus(0, w, 1, 0);
            n++;
        }
    }
    menuOffScreenSetPriority(0);
    _menuPokemon.f24 = 1;
    r = menuSubOpenSelect2(0, ids, n, 0x1BD - lbl_804E7EB8, lbl_804E7EBC + 0x12C, 0, n, 0);
    _menuPokemon.f24 = 0;
    menuOffScreenSetPriority(1);
    return r;
}
}

extern "C" {
void menuPokemonCloseWazaLight(void) {
    menuCloseCustom(0x12, 0, 1);
}
}

extern "C" {
s32 menuPokemonCtrlWaza(void) {
    return 0;
}
}

extern "C" {
void menuPokemonInitWork(void) {
    memset(&_menuPokemon, 0, sizeof(_menuPokemon));
}
}


#include "xd_party_hook.inc"
