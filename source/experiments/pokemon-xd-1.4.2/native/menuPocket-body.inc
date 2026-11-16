extern "C" int WiidsXD_BattleNavigationActive();
#pragma peephole off
#pragma pool_strings off
#include <dolphin/types.h>
#include <game/pcspare.h>
#include "game/gc_fused.h"
#include <game/pcret.h>
#include <game/gcptr.h>
#include <game/statusval.h>
#include <game/shared/menu_work.h>

enum GSgfxLayerID { GSgfxLayerID0 };

struct MenuItem {
    u8 pad0[6];
    s16 x;
    s16 y;
    s16 w;
    s16 h;
};

struct Hero {
public:
    bool itemSort(u8 kind);
};

struct CURSOR {
    s16 x;
    s16 y;
};
typedef CURSOR CursorPos;

typedef s32 (*ActionFunc)(int, int, int*);
typedef s32 (*UseFunc)(u16, int*);

struct SActionMenuItem {
    u32 msg;
    ActionFunc func;
    u16 flags;
    u16 pad;
};

struct TabEntry {
    u8 kind;
    u8 irekae;
    u8 sort;
    u8 pad3;
    u32 spr4;
    s32 activeSpr;
    void (*print)(GSgfxLayerID, int, int, int, int);
    u16 cursorIdx;
    u16 pad12;
    u32 titleMsg;
    const SActionMenuItem* field;
    const SActionMenuItem* battle;
};

struct ResizeEntry {
    u32 id;
    s16 y;
    s16 h;
    u8 resize;
    u8 pad9[3];
};

struct ActionMenuParam {
    const SActionMenuItem* menu;
    int n;
};

struct NumRow {
    s32 a;
    s32 b;
    s32 c;
};

struct NumEntry {
    s32 id;
    const NumRow* tbl;
    s32 count;
};

struct NumberParam {
    int id;
    int cur;
    int price;
};

static inline int pow10(int n)
{
    int r = 1;
    for (int i = 0; i < n; i++) {
        r *= 10;
    }
    return r;
}

static inline int nines(int n)
{
    int r = 0;
    for (int i = 0; i < n; i++) {
        r = r * 10 + 9;
    }
    return r;
}

struct PocketData {
    u8 open;
    u8 pad1[3];
    s32 mode;
    void* trainer;
    Hero* hero;
    s32 pokeSlot;
    s32 curPoke;
    f32 tabScroll;
    f32 listScroll;
    u8 scrollFlag;
    u8 pad21[3];
    f32 roll;
    u32 explMsg;
    s32 result;
    s32 shop;
    s32 number;
    s32 exchangeFrom;
    bool inList;
    u8 pad3d[3];
    s32 tab;
    s32 lastSlot;
    f32 blink;
    u8 resizeInit;
    u8 pad4d[3];
};

#ifdef __MWERKS__
#define POCKET_DATA_CLEAR 0x4C
#else
#include <stddef.h>
#define POCKET_DATA_CLEAR offsetof(PocketData, resizeInit)
#endif

struct ScriptEntry {
    u16 id;
    u16 pad;
    u32 value;
};

struct PocketScript {
    ScriptEntry e[8];
    s32 count;
};

extern "C" {
void* memset(void*, int, u32);
void* memmove(void*, const void*, u32);
CursorPos cursorBiosGetPos(u32 index);
CursorPos cursorBiosSetPos(u16 index, CursorPos newPos);
s32 winMsgOpen(s8 type, u32 msg, bool a, bool b);
s32 winMsgOpenWithSE(s8 type, u32 msg, bool a, bool b, u8 se);
void winMsgClose(bool flag);
void msgctrlSetValue(s32 id, GCuptr value);
u8* itemDataBiosGetPtr(u16 index);
u32 itemDataBiosGetBattleUseFunc(u8* p);
u32 itemDataBiosGetFieldUseFunc(u8* p);
u16 itemDataBiosGetPrice(u8* p);
u32 itemDataBiosGetDoc(u8* p);
u32 itemDataBiosGetName(u8* p);
u8 itemDataBiosGetKind(u8* p);
u8 itemDataBiosGetDeposit(u8* p);
u8 itemDataBiosGetImportant(u8* p);
u8 itemDataBiosGetHidenMachineNo(u8* p);
u32 itemDataBiosGetWazaMachineNo(u8* p);
u8 itemDataBiosGetKinomiNo(u16 itemId);
u32 itemDataBiosGetWazaIDByWazaMachineNo(PCARG(u8, u32) machineNo);
MenuItem* menuItemBiosGetPtr(s32 menuID, u32 index);
#ifdef __MWERKS__
s32 menuShopGetMsgID(u8* out, s32 shop, ...);
#else
u32 menuShopGetMsgID(u8* se, int shop, int code, int arg, ...);
#endif
s8 menuSubOpenYesNo(s32, s32, s32, PCARG(bool, s32));
u8 GSsndPlay(u32 id, s32 time, u32 flag);
void heroAddPokedoru(Hero* hero, u32 offset);
StatusU32 heroGetStatus(Hero* hero, u32 selector, u16 index);
s32 heroItemDecItemDataId(Hero* hero, PCARG(u16, u32) itemID, PCARG(u16, u32) amount, PCARG(s16, u32) index);
void* heroItemGetItemKindToItemAryPtr(Hero* hero, u8 kind, u16* outCount, u16* outMax, u8* f1, u8* f2);
void* heroHizukiItemGetItemAryPtr(Hero* hero, u16* outCount, u16* outMax, u8* f1, u8* f2);
u8 itemCheckValid(void* item);
u16 itemBiosGetItemDataId(void* p);
u16 itemBiosGetNum(void* p);
u16 pcboxGetItemCapacity(void* pcbox, u16 id);
void pcboxAddItem(void* pcbox, u16 id, u16 n);
#ifdef __MWERKS__
s32 menuOpenCustom(s32 id, ...);
#else
s32 menuOpenCustom(s32 menu_id, s32 parent_id, s32* cursor_out, u32 close_flags, u8 check_cursor, s32 open_param, ...);
#endif
void menuClose(s32 id);
void menuCloseSync(s32 id, bool sync);
void menuCloseCustom(s32 id, s32 a, bool b);
s32 windowGetActiveID(void);
s32 menuPokemonOpenItemGive(u8 kind, u8 mode, u16 item, s32 a);
s32 menuPokemonOpenItemUse(s32 a, s32 b, u16 item, void* trainer);
void menuPokemonClose(void);
u8 menuOffScreenSetPriority(u8 val);
void GSmsgPrint2(s32 layer, s32 x, s32 y, u32 color, u32 msg);
u32 GSmsgGetRect(u32 msg);
void* wazaDataBiosGetPtr(u16 id);
u32 wazaDataBiosGetName(void* p);
void fadeSet(s32 a, f32 b);
void fadeCheck(s32 a);
u8 GSfloorPop(void);
void floorSetFadeScript(s32 a, s32 b);
s32 winSpriteGetLayerID(tagWINDOW_WORK* w, tagSPRITE_WORK* sp);
void* fightTrainerBiosGetFightPokemonPtr(void* trainer, u16 index);
void* fightPokemonGetPokemonPtr(void* fp);
void GSmsgPrintRight(s32 layer, s32 x, s32 y, s32 w, s32 h, u32 color, u32 msg);
u16 windowGetKeyRepeat(void);
void winSeqSetMenu(s32 menuID, u32 id);
int menuPocketInputNumber(tagWINDOW_WORK* w, int value, int max, u16 keys, int digits, u32 se, u32 unused);
u8 fn_8002208C(tagWINDOW_WORK* w);
u8 fn_80022128(tagWINDOW_WORK* w);
void windowDrawSpriteCursor(s16 x, s16 y, u32 color0, void* ctx, u16 key, u32 flags, s32 layer, f32* off, u32 color1);
void menuSubCursorAnimeValGet(f32* pos, s32* color);
void menuSubCursorAnimeStart(void);
void menuSubCursorAnimeInit(void);
void winSpriteSetDisp(tagSPRITE_WORK* sp, int on);
void spriteSetScissor(s32 layer, s32 x, s32 y, s32 w, s32 h);
void spriteClearScissor(s32 layer);
void spriteSetEnv(s32 layer);
f32 timeGetLastFrameSec(void);
void menuSubDrawCursorBack2(s32 layer, s32 x, s32 y, s32 w, s32 h, u8 alpha, u8 type, s32 colorKind);
bool winSeqCheckMove(s32 menuID);
u16 windowGetKeyTrg(void);
void menuButtonNormal(tagWINDOW_WORK* w);
void menuSubCursorAnimeMain(void);
void* fightTrainerBiosGetHeroPtr(void* trainer);
u8 GSfloorPush(u32 id);
void floorWaitForChange(void);
void menuPocketBattleDiskMain(s32 a, void* trainer, Hero* hero);
s32 menuPocketCologne(s32 a);
}

void menuPtPlaySe(u32 id);
void menuPocketToolFloatToZero(f32& v, f32 step);

namespace {
void _infoOnly(u32 msg);
void _infoBeep(u32 msg);
int _makeActMenu(SActionMenuItem* out, int a, int b, int c);
void _setActMenuResizeWork(void);
int _execDeposit(int tab, u16 item, int pos);
int _execSell(int tab, u16 item, int pos);
bool _execGive(int tab, u16 item, int pos);
int _execActionMenu(int tab, u16 item, int pos, u16* out);
int _afterDecideItem(int mode, int tab, u16 item, int pos, u16* out);
bool _depositPC(u16 item, int n);
int _getMoney(void);
void _swapItemSlot(int tab, int from, int to);
void _sortItemsThisTab(u8 kind);
void _decItem(u16 item, int kind, int pos, int n);
int _actionMenuOpen(const SActionMenuItem* menu, int n);
int _openNumberInputMenu(int a, int cur, int digits, int price);
int _getItemPrice(int item);
u8 _getItemKind(int item);
u8 _getItemDeposit(u16 item);
UseFunc _getUseFunc(u32 item);
u16 _getItemNumFromMenuPos(int tab, int pos);
int _getStockMaxFromTagID(int tab);
u32 _getItemNameMsg(u32 item);
int _getNbItemSlot(int tab);
s32 _actSuteruNormal(int tab, int pos, int* out);
bool _openPokemonMenuForGiveItem(u16 item, u8 kind, u8 mode);
s32 _actMotaseruNormal(int tab, int pos, int* out);
s32 _actTukauNormal(int tab, int pos, int* out);
void _printItemDouguBall(GSgfxLayerID layer, int x, int y, int tab, int pos);
void _printItemWazaMachine(GSgfxLayerID layer, int x, int y, int tab, int pos);
void _printItemKinomi(GSgfxLayerID layer, int x, int y, int tab, int pos);
void _printItemTaisetsu(GSgfxLayerID layer, int x, int y, int tab, int pos);
void _printItemList(GSgfxLayerID layer, int x, int tab, CURSOR& cursor);
void _setExplanationMsgID(u32 msg);
u32 _getItemInstMsg(u32 item);
u16 _getItemIDFromMenuPos(int tab, int pos);
u16 _selectItem(int& pos, s32* param);
u16 _menuPocketLoop(void);
u8 _IsSortAble(tagWINDOW_WORK* w);
u8 _IsIrekaeAble(tagWINDOW_WORK* w);
}
static int _actionMenuLineHeight(tagWINDOW_WORK* w);

namespace {
static  SActionMenuItem ActionMenuDouguField[4] ;
static  SActionMenuItem ActionMenuBallField[3] ;
static  SActionMenuItem ActionMenuWazaMachineField[3] ;
static  SActionMenuItem ActionMenuKinomiField[4] ;
static  SActionMenuItem ActionMenuTaisetuField[2] ;
static  SActionMenuItem ActionMenuDouguBattle[2] ;
static  SActionMenuItem ActionMenuBallBattle[2] ;
static  SActionMenuItem ActionMenuWazaMachineBattle[1] ;
static  SActionMenuItem ActionMenuKinomiBattle[2] ;
static  SActionMenuItem ActionMenuTaisetuBattle[1] ;

static  TabEntry TabTbl[5] ;

static  NumRow NumWinItem2[2] ;
static  NumRow NumWinItem3[3] ;
static  NumRow NumPriceWinItem2[2] ;
static  NumRow NumPriceWinItem3[3] ;
static  NumEntry MenuPocket2Num[4] ;

ResizeEntry ActMenuResizeWork[7] ;
}
extern "C" {
#ifdef __MWERKS__
u16 item_ball_sort[12] = {
#else
u16 item_ball_sort[12 + 32] = {
#endif
    0x4, 0x3, 0x2, 0xA, 0x9, 0x8, 0x7, 0x6,
    0xC, 0xB, 0x1, 0x5,
};
}
extern "C" {
#ifdef __MWERKS__
u16 item_dougu_sort[128] = {
#else
u16 item_dougu_sort[128 + PC_SPARE_ITEMS] = {
#endif
    0xD, 0x16, 0x15, 0x14, 0x13, 0x1A, 0x1B, 0x1C,
    0x1D, 0xE, 0x12, 0x11, 0xF, 0x10, 0x17, 0x26,
    0x18, 0x19, 0x1E, 0x1F, 0x20, 0x21, 0x2D, 0x22,
    0x23, 0x24, 0x25, 0x49, 0x4A, 0x4B, 0x4C, 0x4D,
    0x4E, 0x4F, 0x50, 0x51, 0x27, 0x28, 0x29, 0x2A,
    0x2B, 0x55, 0x56, 0x53, 0x54, 0x3F, 0x40, 0x41,
    0x43, 0x46, 0x42, 0x44, 0x45, 0x47, 0x5F, 0x60,
    0x61, 0x62, 0x5D, 0x5E, 0xB4, 0xB9, 0x2C, 0xBD,
    0xB6, 0xB5, 0xB8, 0xC3, 0xC5, 0xBE, 0xC2, 0xC8,
    0xDB, 0xB3, 0xB7, 0xBB, 0xC4, 0xBA, 0xC6, 0xD9,
    0xD7, 0xD1, 0xD0, 0xCD, 0xD4, 0xCF, 0xD3, 0xCB,
    0xD2, 0xD6, 0xBC, 0xCC, 0xD5, 0xD8, 0xCE, 0xC7,
    0xDC, 0xDD, 0xC9, 0xCA, 0xE1, 0xE0, 0xDE, 0xDF,
    0xDA, 0xC0, 0xC1, 0xBF, 0xFE, 0xFF, 0x100, 0x101,
    0x102, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A, 0x68, 0x67,
    0x6F, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x1FF,
};
}

extern "C" {
PocketScript menuPocketWork;
}
#ifdef __MWERKS__
PocketData Data;
#else

static PocketData Data;
#endif

extern "C" {
void menuPocket2AddItemScript(u32 id, u32 value)
{    int i;
    for (i = 0; i < 8; i++) {
        ScriptEntry* p = &menuPocketWork.e[i];
        if (p->id == (u16)id && p->value == value) {
            return;
        }
    }
    if (menuPocketWork.count >= 7) {
        return;
    }
    menuPocketWork.e[menuPocketWork.count].id = id;
    menuPocketWork.e[menuPocketWork.count].value = value;
    menuPocketWork.count++;
}
}

extern "C" {
s32 menuPocket2GetItemScript(u16 id)
{
    s32 r = -1;
    int i;
    for (i = 0; i < menuPocketWork.count; i++) {
        if (id == menuPocketWork.e[i].id) {
            r = menuPocketWork.e[i].value;
            break;
        }
    }
    return r;
}
}

extern "C" {
void menuPocketClearItemScript(void)
{
    memset(&menuPocketWork, 0, 0x40);
    menuPocketWork.count = 0;
}
}

extern "C" {
u8 menuPocket2GetLastSlotID(void)
{
    return Data.lastSlot;
}
}

extern "C" {
u16 menuPocket2Open(int mode, int* slot, void* arg)
{
    memset(&Data, 0, POCKET_DATA_CLEAR);
    Data.mode = mode;
    if (mode == 1 || mode == 6) {
        Data.trainer = arg;
        Data.hero = (Hero*)fightTrainerBiosGetHeroPtr(arg);
        int s = *slot;
        Data.pokeSlot = s;
        Data.curPoke = s;
        if (s < 0 || s >= 5) {
            Data.pokeSlot = 0;
        }
    } else {
        Data.hero = (Hero*)arg;
        Data.pokeSlot = -1;
    }
    Data.open = 1;
    GSfloorPush(0x38F);
    floorSetFadeScript(0, 0);
    floorWaitForChange();
    if (Data.result == 0) {
        return 0;
    }
    if (slot != 0) {
        *slot = Data.pokeSlot;
    }
    return Data.result;
}
}

extern "C" {
void menuPocket2Call(int mode, int shop, Hero* hero)
{
    memset(&Data, 0, POCKET_DATA_CLEAR);
    Data.mode = mode;
    Data.hero = hero;
    Data.shop = shop;
    _menuPocketLoop();
}
}

extern "C" {
void menuPocketMain(void)
{
    Data.result = _menuPocketLoop();
}
}

extern "C" {
int menuPocket2Ctrl(tagWINDOW_WORK* w)
{
    switch (w->state) {
    case 0:
        if (w->seqSet == 0) {
            menuPtPlaySe(0x14);
            winSeqSetMenu(0x2C, 0x60);
            CursorPos pos = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
            _setExplanationMsgID(_getItemInstMsg(_getItemIDFromMenuPos(w->cursor, pos.x + pos.y)));
            w->seqSet = 1;
            menuSubCursorAnimeInit();
        }
        break;
    case 2:
        menuPocketToolFloatToZero(Data.tabScroll, 23.0f);
        menuPocketToolFloatToZero(Data.listScroll, 7.0f);
        Data.roll += 0.033333335f;
        if (Data.roll > 1.0f) {
            Data.roll = 0.0f;
        }
        menuSubCursorAnimeMain();
        break;
    case 3:
        if (w->seqSet == 0) {
            menuPtPlaySe(0x15);
            winSeqSetMenu(0x2C, 0x64);
            w->seqSet = 1;
        }
        break;
    }
    return 0;
}
}

extern "C" {
int menuPocket2DrawActiveTab(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    if (sp->id == TabTbl[w->cursor].activeSpr) {
        *(u32*)&sp->r = 0xFFFFFFFF;
    } else {
        sp->alpha = 0;
    }
    return 0;
}
}

extern "C" {
int menuPocket2DrawMoney(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    if (Data.mode != 3) {
        sp->alpha = 0;
    } else {
        sp->alpha = 0xFF;
    }
    return 0;
}
}

extern "C" {
int menuPocket2DrawMoneyBack(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    if (Data.mode != 3) {
        sp->alpha = 0;
    } else {
        sp->alpha = 0xFF;
    }
    return 0;
}
}

extern "C" {
int menuPocket2PrintMoney(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    int money = _getMoney();
    if (Data.mode != 3) {
        return 0;
    }
    msgctrlSetValue(0x50, money);
    s16 width = GSmsgGetRect(0x434E) >> 16;
    GSmsgPrint2(winSpriteGetLayerID(w, sp), sp->w - width, 0, -1, 0x434E);
    return 0;
}
}

extern "C" {
int menuPocket2DrawXHelp(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    u32 disp = 0;
    if (_IsIrekaeAble(w) && Data.inList && fn_80022128(w)) {
        disp = 1;
    }
    winSpriteSetDisp(sp, disp);
    return 0;
}
}

extern "C" {
int menuPocket2DrawRHelp(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    u32 disp = 0;
    if (_IsSortAble(w) && Data.inList && fn_8002208C(w)) {
        disp = 1;
    }
    winSpriteSetDisp(sp, disp);
    return 0;
}
}

extern "C" {
int menuPocket2DrawXHelpText(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    u32 disp = 0;
    if (_IsIrekaeAble(w) && Data.inList && fn_80022128(w)) {
        disp = 1;
    }
    winSpriteSetDisp(sp, disp);
    return 0;
}
}

extern "C" {
int menuPocket2DrawRHelpText(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    u32 disp = 0;
    if (_IsSortAble(w) && Data.inList && fn_8002208C(w)) {
        disp = 1;
    }
    winSpriteSetDisp(sp, disp);
    return 0;
}
}

extern "C" {
void menuPocket2Button(tagWINDOW_WORK* w)
{
    if (Data.exchangeFrom >= 0 || winSeqCheckMove(w->menuID)) {
        return;
    }
    if (windowGetKeyTrg() & 0x10) {
        menuPtPlaySe(0x11);
    }
    menuButtonNormal(w);
}
}

extern "C" {
int menuPocket2Cursor(tagWINDOW_WORK* w)
{
    CursorPos c3;
    CursorPos pos;
    u16 rep = windowGetKeyRepeat();
    u16 trg = windowGetKeyTrg();
    u32 se;
    u8 moved;
    moved = 0;
    se = 0;
    int msgs[7] = { 0x3AA1, 0x3AA2, 0x3AA7, 0x3AA8, 0x3AB7, 0x3AA7, 0x3AA2 };
    if (Data.tabScroll != 0.0f || Data.listScroll != 0.0f || winSeqCheckMove(w->menuID)) {
        return 0;
    }
    CursorPos c;
    c = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    pos.x = c.x;
    pos.y = c.y;
    _getNbItemSlot(w->cursor);
    s32 cur = pos.x + pos.y;
    if (Data.exchangeFrom < 0) {
        if ((trg & 0xC0) && _IsIrekaeAble(w) && _getItemIDFromMenuPos(w->cursor, cur) != 0) {
            Data.exchangeFrom = cur;
            se = 0x11;
        }
    } else if (trg & 0xD0) {
        _swapItemSlot(TabTbl[w->cursor].kind, Data.exchangeFrom, cur);
        se = 0x11;
        Data.exchangeFrom = -1;
    } else if (trg & 0xE0) {
        se = 0x12;
        Data.exchangeFrom = -1;
    }
    CursorPos c2;
    c2 = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    pos.x = c2.x;
    pos.y = c2.y;
    CursorPos prev = pos;
    int total = _getNbItemSlot(w->cursor) + 1;
    if (rep & 2) {
        pos.y += 1;
        if (pos.y + pos.x >= total) {
            pos.y -= 1;
        } else {
            if (pos.y >= 9) {
                pos.x++;
                pos.y--;
                se = 0x10;
                Data.scrollFlag = 1;
            } else {
                se = 0x10;
                Data.scrollFlag = 0;
            }
            Data.listScroll = -30.0f;
        }
        moved = 1;
    }
    if (rep & 1) {
        if (pos.y > 0 || pos.x > 0) {
            pos.y -= 1;
            if (pos.y < 0) {
                pos.y = 0;
                pos.x--;
                se = 0x10;
                Data.scrollFlag = 1;
            } else {
                se = 0x10;
                Data.scrollFlag = 0;
            }
            Data.listScroll = 30.0f;
        }
        moved = 1;
    }
    CursorPos set = pos;
    cursorBiosSetPos(TabTbl[w->cursor].cursorIdx, set);
    if (prev.y != pos.y || prev.x != pos.x) {
        menuSubCursorAnimeStart();
    }
    if (Data.exchangeFrom < 0 && !moved) {
        if ((rep & 8) && Data.mode != 6 && (!WiidsXD_BattleNavigationActive()||w->cursor<4)) {
            if ((u32)++w->cursor >= 5) {
                w->cursor = 0;
            }
            Data.tabScroll = (f32)menuItemBiosGetPtr(0x2C, 0xB6)->w;
            se = 0x44D;
        } else if ((rep & 4) && Data.mode != 6 && (!WiidsXD_BattleNavigationActive()||w->cursor>0)) {
            if (--w->cursor < 0) {
                w->cursor = 4;
            }
            Data.tabScroll = (f32)-menuItemBiosGetPtr(0x2C, 0xB6)->w;
            se = 0x44D;
        } else if (trg & 0x600) {
            if (_IsSortAble(w)) {
                se = 0x11;
                _sortItemsThisTab(TabTbl[w->cursor].kind);
            }
        }
        CursorPos top = *(CursorPos*)&w->top;
        cursorBiosSetPos(2, top);
    }
    if (se != 0) {
        menuPtPlaySe(se);
    }
    int msg;
    if (Data.exchangeFrom >= 0) {
        msg = 0x3AAD;
    } else {
        c3 = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
        pos.x = c3.x;
        pos.y = c3.y;
        u16 id = _getItemIDFromMenuPos(w->cursor, pos.x + pos.y);
        if (id != 0) {
            msg = _getItemInstMsg(id);
        } else {
            msg = msgs[Data.mode];
        }
    }
    _setExplanationMsgID(msg);
    return 0;
}
}

extern "C" {
int menuPocket2DrawLineCursor(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    CursorPos pos = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    int y;
    u8 visible;
    if (Data.exchangeFrom >= 0) {
        y = (Data.exchangeFrom - pos.x) * 30;
        if (Data.scrollFlag != 0) {
            y -= (int)Data.listScroll;
        }
        visible = 0;
        if (y >= 0 && y < 270) {
            visible = 1;
        }
    } else {
        y = pos.y * 30;
        if (Data.scrollFlag == 0) {
            y += (int)Data.listScroll;
        }
        visible = 1;
    }
    if (Data.tabScroll != 0.0f) {
        visible = 0;
    }
    if (visible) {
        int ty = menuItemBiosGetPtr(w->menuID, sp->id)->y;
        ty += y;
        sp->y = ty;
        if (Data.exchangeFrom >= 0) {
            winSpriteSetDisp(sp, 0);
            MenuItem* m = menuItemBiosGetPtr(w->menuID, sp->id);
            menuSubDrawCursorBack2(winSpriteGetLayerID(w, sp), 0, 0, m->w, m->h, sp->alpha, 1, 1);
        } else {
            winSpriteSetDisp(sp, 1);
        }
    } else {
        winSpriteSetDisp(sp, 0);
    }
    return 0;
}
}

extern "C" {
int menuPocket2DrawBallCursor(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    CursorPos pos = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    int y = pos.y * 30;
    if (Data.scrollFlag == 0) {
        y += (s16)Data.listScroll;
    }
    if (((u8)(Data.exchangeFrom >= 0) ^ (u8)(sp->id == 0xB1)) && Data.tabScroll == 0.0f) {
        f32 off[2] = { 0.0f, 0.0f };
        s32 color = -1;
        if (w->menuID == windowGetActiveID()) {
            menuSubCursorAnimeValGet(off, &color);
        }
        windowDrawSpriteCursor(0, y, -1, w, 0xC2, 0, winSpriteGetLayerID(w, sp), off, color);
    }
    winSpriteSetDisp(sp, 0);
    return 0;
}
}

extern "C" {
int menuPocket2DrawExchangeCursor(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    CursorPos pos = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    int y = pos.y * 30;
    if (Data.scrollFlag == 0) {
        y += (s16)Data.listScroll;
    }
    if (Data.exchangeFrom >= 0) {
        MenuItem* item = menuItemBiosGetPtr(w->menuID, sp->id);
        sp->y = item->y + y;
        winSpriteSetDisp(sp, 1);
        static f32 duration = 0.5f;
        Data.blink += timeGetLastFrameSec();
        if (Data.blink > duration) {
            Data.blink = 0.0f;
        }
        f32 t = Data.blink / duration;
        if (t > 0.5f) {
            t = 1.0f - t;
        }
        t *= 2.0f;
        sp->alpha = GC_FUSED(155.0f, t) + 100.0f;
    } else {
        winSpriteSetDisp(sp, 0);
    }
    return 0;
}
}

extern "C" {
int menuPocket2DrawRollUp(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    if (!Data.inList) {
        return 0;
    }
    CursorPos pos = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    if (pos.x > 0) {
        sp->alpha = 255.0f * (1.0f - Data.roll);
    } else {
        sp->alpha = 0;
    }
    return 0;
}
}

extern "C" {
int menuPocket2DrawTitleLR(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    winSpriteSetDisp(sp, Data.mode != 6);
    return 0;
}
}

extern "C" {
int menuPocket2DrawRollDown(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    if (!Data.inList) {
        return 0;
    }
    CursorPos pos = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    if (pos.x + 9 < _getNbItemSlot(w->cursor) + 1) {
        sp->alpha = 255.0f * (1.0f - Data.roll);
    } else {
        sp->alpha = 0;
    }
    return 0;
}
}

extern "C" {
int menuPocket2PrintItemKind(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    int tab;
    MenuItem* area = menuItemBiosGetPtr(w->menuID, 0xB7);
    MenuItem* base = menuItemBiosGetPtr(w->menuID, sp->id);
    f32 off;
    s16 tw2;
    u32 title2;
    spriteSetScissor(winSpriteGetLayerID(w, sp), area->x - base->x, area->y - base->y, area->w, area->h);
    int center = sp->w / 2;
    u32 title = TabTbl[w->cursor].titleMsg;
    s16 tw;
    tw = GSmsgGetRect(title) >> 16;
    GSmsgPrint2(winSpriteGetLayerID(w, sp), (int)Data.tabScroll + (center - tw / 2), 0, -1, title);
    if (Data.tabScroll != 0.0f) {
        if (Data.tabScroll > 0.0f) {
            tab = w->cursor - 1;
            if (tab < 0) {
                tab = 4;
            }
            off = Data.tabScroll - (f32)menuItemBiosGetPtr(0x2C, 0xB6)->w;
        } else {
            tab = w->cursor + 1;
            if ((u32)tab >= 5) {
                tab = 0;
            }
            off = Data.tabScroll + (f32)menuItemBiosGetPtr(0x2C, 0xB6)->w;
        }
        title2 = TabTbl[tab].titleMsg;
        tw2 = GSmsgGetRect(title2) >> 16;
        GSmsgPrint2(winSpriteGetLayerID(w, sp), (int)off + (center - tw2 / 2), 0, -1, title2);
    }
    spriteClearScissor(winSpriteGetLayerID(w, sp));
    spriteSetEnv(winSpriteGetLayerID(w, sp));
    return 0;
}
}

extern "C" {
int menuPocket2PrintItemList(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    MenuItem* area = menuItemBiosGetPtr(w->menuID, 0xB6);
    MenuItem* base = menuItemBiosGetPtr(w->menuID, sp->id);
    spriteSetScissor(winSpriteGetLayerID(w, sp), area->x - base->x, area->y - base->y, area->w, area->h);
    CURSOR cur = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    _printItemList((GSgfxLayerID)winSpriteGetLayerID(w, sp), (int)Data.tabScroll, w->cursor, cur);
    if (Data.tabScroll != 0.0f) {
        int tab;
        f32 off;
        if (Data.tabScroll > 0.0f) {
            tab = w->cursor - 1;
            if (tab < 0) {
                tab = 4;
            }
            off = Data.tabScroll - (f32)menuItemBiosGetPtr(0x2C, 0xB6)->w;
        } else {
            tab = w->cursor + 1;
            if ((u32)tab >= 5) {
                tab = 0;
            }
            off = Data.tabScroll + (f32)menuItemBiosGetPtr(0x2C, 0xB6)->w;
        }
        CURSOR cur2 = cursorBiosGetPos(TabTbl[tab].cursorIdx);
        _printItemList((GSgfxLayerID)winSpriteGetLayerID(w, sp), (int)off, tab, cur2);
    }
    spriteClearScissor(winSpriteGetLayerID(w, sp));
    spriteSetEnv(winSpriteGetLayerID(w, sp));
    return 0;
}
}

extern "C" {
int menuPocket2ActionCtrl(tagWINDOW_WORK* w)
{
    switch (w->state) {
    case 0:
        if (w->seqSet == 0) {
            winSeqSetMenu(0x2D, 0x68);
            Data.blink = 0.0f;
            w->seqSet = 1;
            menuSubCursorAnimeInit();
        }
        break;
    case 3:
        if (w->seqSet == 0) {
            winSeqSetMenu(0x2D, 0x6C);
            w->seqSet = 1;
        }
        break;
    }
    return 0;
}
}

extern "C" {
int menuPocket2ActionCursor(tagWINDOW_WORK* w)
{
    ActionMenuParam* param = (ActionMenuParam*)w->params[0];
    u16 keys = windowGetKeyRepeat();
    CursorPos prev = *(CursorPos*)&w->top;
    int n = param->n;
    if (keys & 2) {
        int v = w->cursor + 1;
        if (v >= n) {
            v = 0;
        }
        w->cursor = (s8)v;
    }
    if (keys & 1) {
        int v = w->cursor - 1;
        if (v < 0) {
            v = n - 1;
        }
        w->cursor = (s8)v;
    }
    if (prev.y != w->cursor || prev.x != w->top) {
        menuSubCursorAnimeStart();
    }
    return 0;
}
}

static int _actionMenuLineHeight(tagWINDOW_WORK* w)
{
    MenuItem* a = menuItemBiosGetPtr(w->menuID, 0xDA);
    int d = a->y - menuItemBiosGetPtr(w->menuID, 0xD9)->y;
    if (d < 0) {
        d = -d;
    }
    return d;
}

extern "C" {
int menuPocket2DrawActionMenu(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    ActionMenuParam* param = (ActionMenuParam*)w->params[0];
    if (param->n < 4) {
        u32 i;
        for (i = 0; i < 7; i++) {
            if ((u32)sp->id == ActMenuResizeWork[i].id) {
                break;
            }
        }
        if (i < 7) {
            int off = _actionMenuLineHeight(w) * (4 - param->n);
            sp->y = off + ActMenuResizeWork[i].y;
            if (ActMenuResizeWork[i].resize != 0) {
                sp->h = ActMenuResizeWork[i].h - off;
            }
        }
    }
    return 0;
}
}

extern "C" {
int menuPocket2DrawActionText(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    const int ids[4] = { 0xE0, 0xDF, 0xDE, 0xDD };
    ActionMenuParam* param = (ActionMenuParam*)w->params[0];
    int idx;
    for (idx = 0; idx < 4; idx++) {
        if (sp->id == ids[idx]) {
            break;
        }
    }
    idx -= 4 - param->n;
    if (idx < 0 || idx >= param->n) {
        return 0;
    }
    GSmsgPrint2(winSpriteGetLayerID(w, sp), 0, 0, -1, param->menu[idx].msg);
    return 0;
}
}

extern "C" {
int menuPocket2DrawActionCursor(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    int ids[4] = { 0xDC, 0xDB, 0xDA, 0xD9 };
    ActionMenuParam* param = (ActionMenuParam*)w->params[0];
    int idx = w->cursor + (4 - param->n);
    if (idx < 0 || idx >= 4) {
        return 0;
    }
    if (sp->id == ids[idx]) {
        f32 pos[2] = { 0.0f, 0.0f };
        s32 color = -1;
        if (w->menuID == windowGetActiveID()) {
            menuSubCursorAnimeValGet(pos, &color);
        }
        windowDrawSpriteCursor(0, 0, -1, w, 0xC2, 0, winSpriteGetLayerID(w, sp), pos, color);
    }
    winSpriteSetDisp(sp, 0);
    return 0;
}
}

extern "C" {
int menuPocket2DrawActionCursorLine(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    ActionMenuParam* param = (ActionMenuParam*)w->params[0];
    int base = 4 - param->n;
    MenuItem* item = menuItemBiosGetPtr(w->menuID, sp->id);
    sp->y = item->y + (w->cursor + base) * _actionMenuLineHeight(w);
    return 0;
}
}

extern "C" {
int menuPocket2NumCtrl(tagWINDOW_WORK* w)
{
    NumberParam* param = (NumberParam*)w->params[0];
    switch (w->state) {
    case 0:
        if (w->seqSet == 0) {
            winSeqSetMenu(param->id, 0x70);
            w->seqSet = 1;
        }
        break;
    case 3:
        if (w->seqSet == 0) {
            winSeqSetMenu(param->id, 0x74);
            w->seqSet = 1;
        }
        break;
    }
    return 0;
}
}

extern "C" {
int menuPocket2NumCursor(tagWINDOW_WORK* w)
{
    NumberParam* param = (NumberParam*)w->params[0];
    u16 keys = windowGetKeyRepeat();
    u32 i;
    for (i = 0; i < 4; i++) {
        if (param->id == MenuPocket2Num[i].id) {
            break;
        }
    }
    if (i >= 4) {
        return 0;
    }
    Data.number = menuPocketInputNumber(w, Data.number, param->cur, keys, MenuPocket2Num[i].count, 0x10, 0x44D);
    return 0;
}
}

extern "C" {
int menuPocketInputNumber(tagWINDOW_WORK* w, int value, int max, u16 keys, int digits, u32 se, u32 unused)
{
    int old;
    int pos = digits - w->cursor - 1;
    if (keys & 0xF) {
        old = value;
        int p = pow10(digits - w->cursor - 1);
        int d0 = value / p % 10;
        int d1 = value / (p * 10) % 10;
        int d2 = value / (p * 100) % 10;
        if (keys & 1) {
            if (max != 0) {
                if (value == max) {
                    value = 0;
                } else if (d0 < 9) {
                    value += p;
                } else if (pos >= digits - 1) {
                    value = 0;
                    for (int i = 0; i < digits; i++) {
                        value *= 10;
                        value += 9;
                    }
                } else if (d1 < 9) {
                    value += p;
                } else if (pos >= digits - 2) {
                    value = 0;
                    for (int i = 0; i < digits; i++) {
                        value *= 10;
                        value += 9;
                    }
                } else if (d2 < 9) {
                    value += p;
                } else {
                    value = 0;
                }
                if (value > max) {
                    value = max;
                }
            }
        } else if (keys & 2) {
            if (max != 0) {
                if (value == 0) {
                    value = max;
                } else if (d0 >= 1) {
                    value -= p;
                } else if (pos >= digits - 1) {
                    value = 0;
                } else if (d1 >= 1) {
                    value -= p;
                } else if (pos >= digits - 2) {
                    value = 0;
                } else if (d2 >= 1) {
                    value -= p;
                } else {
                    value = max;
                }
            }
        } else if (keys & 8) {
            if (++w->cursor >= digits) {
                w->cursor = (s8)(digits - 1);
            }
        } else if (keys & 4) {
            if (--w->cursor < 0) {
                w->cursor = 0;
            }
        }
        if (old != value) {
            menuPtPlaySe(se);
        }
    }
    return value;
}
}

extern "C" {
int menuPocket2PrintNumMenu(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    NumberParam* param;
    u32 i;
    const NumRow* row;
    int j;
    s32 k;
    int power;
    int digit;

    param = (NumberParam*)w->params[0];
    for (i = 0; i < 4; i++) {
        if (param->id == MenuPocket2Num[i].id) {
            break;
        }
    }
    if (i >= 4) {
        return 0;
    }
    row = MenuPocket2Num[i].tbl;
    j = 0;
    while (j < MenuPocket2Num[i].count) {
        if (sp->id == row->a) {
            break;
        }
        j++;
        row++;
    }
    if (j >= MenuPocket2Num[i].count) {
        return 0;
    }
    power = 1;
    for (k = 0; k < MenuPocket2Num[i].count - j - 1; k++) {
        power *= 10;
    }
    digit = Data.number / power;
    digit %= 10;
    msgctrlSetValue(0x34, digit);
    GSmsgPrintRight(winSpriteGetLayerID(w, sp), 0, 0, sp->w, sp->h, -1, 0x4270);
    return 0;
}
}

extern "C" {
int menuPocket2PrintPrice(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    int price = ((NumberParam*)w->params[0])->price;
    if (price <= 0) {
        return 0;
    }
    msgctrlSetValue(0x50, price * Data.number);
    s16 width = GSmsgGetRect(0x434F) >> 16;
    GSmsgPrint2(winSpriteGetLayerID(w, sp), sp->w - width, 0, -1, 0x434F);
    return 0;
}
}

extern "C" {
int menuPocket2DrawNumMenuCursor(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    NumberParam* param = (NumberParam*)w->params[0];
    u32 i;
    for (i = 0; i < 4; i++) {
        if (param->id == MenuPocket2Num[i].id) {
            break;
        }
    }
    if (i >= 4) {
        return 0;
    }
    if (w->cursor < 0 || w->cursor >= MenuPocket2Num[i].count) {
        return 0;
    }
    const NumRow* row = MenuPocket2Num[i].tbl;
    row += w->cursor;
    if (row->b == sp->id || row->c == sp->id) {
        sp->alpha = 0xFF;
    } else {
        sp->alpha = 0;
    }
    return 0;
}
}

extern "C" {
s32 menuPocket2OpenPokemonMenu(u16 item, int use)
{
    int mode;
    s32 r;
    menuCloseCustom(0x2C, 0, 1);
    if (Data.mode == 1 || Data.mode == 6) {
        mode = 4;
    } else {
        mode = 3;
    }
    r = menuPokemonOpenItemUse(mode, (bool)use, (u16)item, Data.trainer);
    if (use == 0) {
        r = -1;
    }
    return r;
}
}

extern "C" {
void menuPocket2ClosePokemonMenu(s32 slot)
{
    Data.pokeSlot = slot;
    menuPokemonClose();
}
}

extern "C" {
void menuPocket2GetTemochiPokemon(u32 slot, void** pokemon, void** fightPokemon)
{
    void* fp;
    void* p;
    if (Data.mode == 1 || Data.mode == 6) {
        fp = fightTrainerBiosGetFightPokemonPtr(Data.trainer, slot);
        p = fightPokemonGetPokemonPtr(fp);
    } else {
        p = (void*)heroGetStatus(Data.hero, 3, slot);
        fp = 0;
    }
    *pokemon = p;
    *fightPokemon = fp;
}
}

extern "C" {
s32 menuPocket2GetCurrentTemochiPokemonSlotID(void)
{
    return Data.pokeSlot = Data.curPoke;
}
}

extern "C" {
void menuPocket2GetHero(Hero** hero, void** trainer)
{
    *hero = Data.hero;
    *trainer = Data.trainer;
}
}

extern "C" {
int menuPocket2PrintExplanation(tagWINDOW_WORK* w, tagSPRITE_WORK* sp)
{
    cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    u32 msg = Data.explMsg;
    if (msg != 0) {
        GSmsgPrint2(winSpriteGetLayerID(w, sp), 0, 0, -1, msg);
    }
    return 0;
}
}

namespace {
u8 _IsIrekaeAble(tagWINDOW_WORK* w)
{
    if (Data.mode == 3 || Data.mode == 4 || Data.mode == 5) {
        return 0;
    }
    return TabTbl[w->cursor].irekae;
}
}

namespace {
u8 _IsSortAble(tagWINDOW_WORK* w)
{
    if (Data.mode == 3 || Data.mode == 4 || Data.mode == 5) {
        return 0;
    }
    return TabTbl[w->cursor].sort;
}
}

namespace {
static CursorPos xddt_local1_czero;
u16 _menuPocketLoop(void)
{
    ;
    CursorPos c;
    CursorPos cp;
    s32 param;
    s32* pp;
    u16 out;
    u16 item;
    int i;
    memset(&cp, 0, 4);
    if (Data.mode == 6) {
        cp.y = 1;
    } else {
        c = cursorBiosGetPos(2);
        cp.x = c.x;
        cp.y = c.y;
    }
    pp = &param;
    param = cp.y;
    for (i = 0; i < 5; i++) {
        CursorPos c = cursorBiosGetPos(TabTbl[i].cursorIdx);
        if (c.x + c.y >= _getNbItemSlot(i) + 1) {
            CursorPos z = xddt_local1_czero;
            cursorBiosSetPos(TabTbl[i].cursorIdx, z);
        }
    }
    if (!Data.resizeInit) {
        _setActMenuResizeWork();
    }
    int pos = 0;
    for (;;) {
        Data.inList = 1;
        item = _selectItem(pos, pp);
        Data.inList = 0;
        pp = 0;
        if (item == 0) {
            break;
        }
        int r = _afterDecideItem(Data.mode, Data.tab, item, pos, &out);
        if (out != 0) {
            item = out;
        }
        switch (r) {
        case 0:
            continue;
        case 1:
            break;
        default:
            continue;
        }
        break;
    }
    menuCloseCustom(0x2C, 0, 1);
    if (Data.mode == 4) {
        fadeSet(3, 0.5f);
        fadeCheck(1);
    }
    if (Data.open) {
        menuOffScreenSetPriority(1);
        GSfloorPop();
        floorSetFadeScript(0, 0);
    }
    return item;
}
}

namespace {
u16 _selectItem(int& pos, s32* param)
{
    Data.exchangeFrom = -1;
    int tab = menuOpenCustom(0x2C, windowGetActiveID(), param, 0, 1, 0);
    if (tab == -1) {
        return 0;
    }
    Data.tab = tab;
    CursorPos cp = cursorBiosGetPos(TabTbl[tab].cursorIdx);
    pos = cp.x + cp.y;
    return _getItemIDFromMenuPos(tab, pos);
}
}

namespace {
u16 _getItemIDFromMenuPos(int tab, int pos)
{
    u16 count;
    u8* items;
    int found;
    int i;
    items = (u8*)heroItemGetItemKindToItemAryPtr(Data.hero, TabTbl[tab].kind, &count, 0, 0, 0);
    found = -1;
    for (i = 0; i < count; i++, items += 4) {
        if (itemCheckValid(items)) {
            found++;
            if (found >= pos) {
                return itemBiosGetItemDataId(items);
            }
        }
    }
    return 0;
}
}

namespace {
u32 _getItemInstMsg(u32 item)
{
    return itemDataBiosGetDoc(itemDataBiosGetPtr(item));
}
}

namespace {
void _setExplanationMsgID(u32 msg)
{
    Data.explMsg = msg;
}
}

namespace {
void _printItemList(GSgfxLayerID layer, int x, int tab, CURSOR& cursor)
{
    int n;
    int y;
    const TabEntry* t = &TabTbl[tab];
    n = _getNbItemSlot(tab);
    int idx = cursor.x;
    int start = 0;
    int maxLines = 9;
    int scroll = 0;
    int i;
    if (Data.listScroll != 0.0f && Data.scrollFlag != 0) {
        if (Data.listScroll < 0.0f) {
            idx--;
            start = -1;
        } else {
            maxLines = 10;
        }
        scroll = Data.listScroll;
    }
    y = start * 30;
    i = start;
    for (; i < maxLines && idx < n; i++, idx++) {
        if (idx >= 0) {
            t->print(layer, x, y - scroll, tab, idx);
        }
        y += 30;
    }
    if (i < maxLines) {
        u32 msg;
        y = i * 30 - scroll;
        if (Data.mode == 4) {
            msg = 0x3BBA;
        } else {
            msg = 0x3A9E;
        }
        GSmsgPrint2(layer, x, y, -1, msg);
    }
}
}

namespace {
void _printItemTaisetsu(GSgfxLayerID layer, int x, int y, int tab, int pos)
{
    u32 name = _getItemNameMsg(_getItemIDFromMenuPos(tab, pos));
    if (name != 0) {
        GSmsgPrint2(layer, x, y, -1, name);
    }
}
}

namespace {
void _printItemKinomi(GSgfxLayerID layer, int x, int y, int tab, int pos)
{
    GSmsgPrint2(layer, x, y, -1, 0x3A9F);
    int px = x + (GSmsgGetRect(0x3A9F) >> 16);
    u16 id = _getItemIDFromMenuPos(tab, pos);
    u8 num = itemDataBiosGetKinomiNo(id) + 1;
    if (num < 10) {
        msgctrlSetValue(0x34, 0);
        GSmsgPrint2(layer, px, y, -1, 0x4270);
        px += GSmsgGetRect(0x4270) >> 16;
    }
    msgctrlSetValue(0x34, num);
    GSmsgPrint2(layer, px, y, -1, 0x4270);
    px += GSmsgGetRect(0x4270) >> 16;
    u32 name = _getItemNameMsg(id);
    GSmsgPrint2(layer, px + 9, y, -1, name);
    int max = _getStockMaxFromTagID(tab);
    msgctrlSetValue(0x34, max);
    s16 w = GSmsgGetRect(0x4270) >> 16;
    w += (s16)(GSmsgGetRect(0x428F) >> 16);
    GSmsgPrint2(layer, x + menuItemBiosGetPtr(0x2C, 0xB4)->w - w, y, -1, 0x428F);
    msgctrlSetValue(0x34, _getItemNumFromMenuPos(tab, pos));
    w = GSmsgGetRect(0x4270) >> 16;
    GSmsgPrint2(layer, x + menuItemBiosGetPtr(0x2C, 0xB4)->w - w + 1, y, -1, 0x4270);
}
}

namespace {
void _printItemWazaMachine(GSgfxLayerID layer, int x, int y, int tab, int pos)
{
    int px;
    s16 w2;
    u8* item = itemDataBiosGetPtr(_getItemIDFromMenuPos(tab, pos));
    u8 machine = itemDataBiosGetWazaMachineNo(item);
    int num = machine + 1;
    u8 hiden = itemDataBiosGetHidenMachineNo(item);
    u8 hidenNum = hiden + 1;
    u32 label;
    if (hiden != 0xFF) {
        label = 0x3AA0;
    } else {
        label = 0x3A9F;
    }
    GSmsgPrint2(layer, x, y, -1, label);
    px = x + (GSmsgGetRect(label) >> 16);
    if (hiden != 0xFF) {
        msgctrlSetValue(0x34, hidenNum);
        GSmsgPrint2(layer, px, y, -1, 0x4270);
        px += GSmsgGetRect(0x4270) >> 16;
    } else {
        if (num < 10) {
            msgctrlSetValue(0x34, 0);
            GSmsgPrint2(layer, px, y, -1, 0x4270);
            px += GSmsgGetRect(0x4270) >> 16;
        }
        msgctrlSetValue(0x34, num);
        GSmsgPrint2(layer, px, y, -1, 0x4270);
        px += GSmsgGetRect(0x4270) >> 16;
    }
    u32 name = wazaDataBiosGetName(wazaDataBiosGetPtr(itemDataBiosGetWazaIDByWazaMachineNo(machine)));
    GSmsgPrint2(layer, px + 9, y, -1, name);
    int max = _getStockMaxFromTagID(tab);
    msgctrlSetValue(0x34, max);
    s16 w = GSmsgGetRect(0x4270) >> 16;
    w += (s16)(GSmsgGetRect(0x428F) >> 16);
    GSmsgPrint2(layer, x + menuItemBiosGetPtr(0x2C, 0xB4)->w - w, y, -1, 0x428F);
    msgctrlSetValue(0x34, _getItemNumFromMenuPos(tab, pos));
    w2 = GSmsgGetRect(0x4270) >> 16;
    GSmsgPrint2(layer, x + menuItemBiosGetPtr(0x2C, 0xB4)->w - w2 + 1, y, -1, 0x4270);
}
}

namespace {
void _printItemDouguBall(GSgfxLayerID layer, int x, int y, int tab, int pos)
{
    u32 name = _getItemNameMsg(_getItemIDFromMenuPos(tab, pos));
    if (name != 0) {
        GSmsgPrint2(layer, x, y, -1, name);
    }
    int max = _getStockMaxFromTagID(tab);
    msgctrlSetValue(0x34, max);
    s16 w = GSmsgGetRect(0x4270) >> 16;
    w += (s16)(GSmsgGetRect(0x428F) >> 16);
    GSmsgPrint2(layer, x + menuItemBiosGetPtr(0x2C, 0xB4)->w - w, y, -1, 0x428F);
    msgctrlSetValue(0x34, _getItemNumFromMenuPos(tab, pos));
    s16 w2 = GSmsgGetRect(0x4270) >> 16;
    GSmsgPrint2(layer, x + menuItemBiosGetPtr(0x2C, 0xB4)->w - w2 + 1, y, -1, 0x4270);
}
}

namespace {
s32 _actTukauNormal(int tab, int pos, int* out)
{
    u16 raw = _getItemIDFromMenuPos(tab, pos);
    u32 id = raw;
    s32 ret;
    UseFunc f = _getUseFunc(id);
    if (f == 0) {
        _infoBeep(0xC374);
        *out = 0;
        return 1;
    }
    switch (f(id, out)) {
    case 0:
        ret = 0;
        break;
    case 1:
        ret = 1;
        break;
    case 2:
        if (tab == 4 && raw != 0x1F6) {
            ret = 0;
        } else {
            ret = 2;
        }
        break;
    }
    return ret;
}
}

namespace {
s32 _actMotaseruNormal(int tab, int pos, int* out)
{
    u16 id = _getItemIDFromMenuPos(tab, pos);
    if (itemDataBiosGetHidenMachineNo(itemDataBiosGetPtr(id)) != 0xFF) {
        msgctrlSetValue(0x2D, id);
        _infoBeep(0xC37A);
        *out = 0;
        return 0;
    }
    u8 kind = _getItemKind(id);
    if (!_openPokemonMenuForGiveItem(id, kind, pos)) {
        return 1;
    }
    *out = 0;
    return 0;
}
}

namespace {
bool _openPokemonMenuForGiveItem(u16 item, u8 kind, u8 mode)
{
    menuCloseCustom(0x2C, 0, 1);
    Data.pokeSlot = menuPokemonOpenItemGive(kind, mode, item, 0);
    menuOffScreenSetPriority(0);
    menuPokemonClose();
    return Data.pokeSlot >= 0;
}
}

namespace {
s32 _actSuteruNormal(int tab, int pos, int* out)
{
    const SActionMenuItem yesNo[2] = {
        { 0x426A, 0, 0, 0 },
        { 0x426B, 0, 0, 0 },
    };
    _setExplanationMsgID(0x3AAA);
    int digits;
    if (_getStockMaxFromTagID(tab) > 100) {
        digits = 3;
    } else {
        digits = 2;
    }
    int n = _openNumberInputMenu(1, _getItemNumFromMenuPos(tab, pos), digits, 0);
    if (n < 0) {
        *out = 0;
        return 0;
    }
    u16 id = _getItemIDFromMenuPos(tab, pos);
    msgctrlSetValue(0x2D, id);
    msgctrlSetValue(0x2F, n);
    _setExplanationMsgID(0x3AAB);
    int sel = _actionMenuOpen(yesNo, 2);
    if (sel == 1 || sel == -1) {
        *out = 0;
        return 0;
    }
    msgctrlSetValue(0x2D, id);
    msgctrlSetValue(0x2F, n);
    _infoOnly(0xC379);
    *out = n;
    return 0;
}
}

namespace {
int _getNbItemSlot(int tab)
{
    u16 count;
    u8* items;
    int kind = TabTbl[tab].kind;
    if (kind >= 0) {
        items = (u8*)heroItemGetItemKindToItemAryPtr(Data.hero, kind, &count, 0, 0, 0);
    } else {
        items = (u8*)heroHizukiItemGetItemAryPtr(Data.hero, &count, 0, 0, 0);
    }
    int i;
    int n = 0;
    for (i = 0; i < count; i++, items += 4) {
        if (itemCheckValid(items)) {
            n++;
        }
    }
    return n;
}
}

namespace {
u32 _getItemNameMsg(u32 item)
{
    return itemDataBiosGetName(itemDataBiosGetPtr(item));
}
}

namespace {
int _getStockMaxFromTagID(int tab)
{
    u16 max;
    heroItemGetItemKindToItemAryPtr(Data.hero, TabTbl[tab].kind, 0, &max, 0, 0);
    return max;
}
}

namespace {
u16 _getItemNumFromMenuPos(int tab, int pos)
{
    u16 count;
    u8* items;
    int kind = TabTbl[tab].kind;
    if (kind >= 0) {
        items = (u8*)heroItemGetItemKindToItemAryPtr(Data.hero, kind, &count, 0, 0, 0);
    } else {
        items = (u8*)heroHizukiItemGetItemAryPtr(Data.hero, &count, 0, 0, 0);
    }
    int i;
    int found = -1;
    for (i = 0; i < count; i++, items += 4) {
        if (itemCheckValid(items)) {
            found++;
            if (found >= pos) {
                return itemBiosGetNum(items);
            }
        }
    }
    return 0;
}
}

namespace {
UseFunc _getUseFunc(u32 item)
{
    u8* p = itemDataBiosGetPtr(item);
    if (Data.mode == 0) {
        return (UseFunc)itemDataBiosGetFieldUseFunc(p);
    }
    return (UseFunc)itemDataBiosGetBattleUseFunc(p);
}
}

namespace {
u8 _getItemDeposit(u16 item)
{
    return itemDataBiosGetDeposit(itemDataBiosGetPtr(item));
}
}

namespace {
u8 _getItemKind(int item)
{
    return itemDataBiosGetKind(itemDataBiosGetPtr(item));
}
}

namespace {
int _getItemPrice(int item)
{
    return itemDataBiosGetPrice(itemDataBiosGetPtr(item));
}
}

namespace {
int _openNumberInputMenu(int a, int cur, int digits, int price)
{
    NumberParam param;
    int digitArg;
    int id;
    if (cur < 0) {
        return 0;
    }
    if (price > 0) {
        if (digits == 2) {
            id = 0x30;
            digitArg = 1;
        } else {
            id = 0x31;
            digitArg = 2;
        }
    } else {
        if (digits == 2) {
            id = 0x2E;
            digitArg = 1;
        } else {
            id = 0x2F;
            digitArg = 2;
        }
    }
    param.id = id;
    param.cur = cur;
    param.price = price;
    Data.number = a;
    int r = menuOpenCustom(id, windowGetActiveID(), &digitArg, 0, 1, 1, &param);
    menuClose(id);
    menuCloseSync(id, 1);
    if (r == -1 || Data.number == 0) {
        return -1;
    }
    return Data.number;
}
}

namespace {
int _actionMenuOpen(const SActionMenuItem* menu, int n)
{
    ActionMenuParam param;
    param.menu = menu;
    param.n = n;
    int sel = menuOpenCustom(0x2D, windowGetActiveID(), 0, 0, 1, 1, &param);
    menuClose(0x2D);
    menuCloseSync(0x2D, 1);
    if (sel == -1) {
        return -1;
    }
    if (sel < 0 || sel >= n) {
        return -1;
    }
    return sel;
}
}

namespace {
void _decItem(u16 item, int kind, int pos, int n)
{
    heroItemDecItemDataId(Data.hero, item, n, pos);
}
}

namespace {
void _sortItemsThisTab(u8 kind)
{
    Data.hero->itemSort(kind);
}
}

namespace {
struct ItemSlot {
    u16 id;
    u16 n;
};
void _swapItemSlot(int kind, int from, int to)
{
    u16 count;
    if (from == to || from == to - 1) {
        return;
    }
    ItemSlot* items = (ItemSlot*)heroItemGetItemKindToItemAryPtr(Data.hero, kind, &count, 0, 0, 0);
    ItemSlot saved = items[from];
    if (from != count - 1) {
        memmove(items + from, items + from + 1, (count - from - 1) * 4);
    }
    if (from < to) {
        to--;
    }
    memmove(items + to + 1, items + to, (count - to - 1) * 4);
    items[to].id = saved.id;
    items[to].n = saved.n;
}
}

namespace {
int _getMoney(void)
{
    return heroGetStatus(Data.hero, 0xC, 0);
}
}

namespace {
bool _depositPC(u16 item, int n)
{
    if (pcboxGetItemCapacity(0, item) < n) {
        return false;
    }
    pcboxAddItem(0, item, n);
    return true;
}
}

namespace {
int _afterDecideItem(int mode, int tab, u16 item, int pos, u16* out)
{
    u16 result;
    int ret = mode;
    result = 0;
    switch (mode) {
    case 0:
    case 1:
    case 6: {
        int r = _execActionMenu(tab, item, pos, &result);
        if (r == 2) {
            ret = 1;
        } else if (mode == 0) {
            ret = 0;
        } else if (r == 0) {
            ret = 1;
        } else {
            ret = 0;
        }
        break;
    }
    case 2:
    case 5:
        if (_execGive(tab, item, pos)) {
            ret = 1;
        } else {
            ret = 0;
        }
        break;
    case 3:
        _execSell(tab, item, pos);
        ret = 0;
        break;
    case 4:
        _execDeposit(tab, item, pos);
        ret = 0;
        break;
    }
    *out = result;
    return ret;
}
}

namespace {
#pragma push
#pragma optimize_for_size on
int _execActionMenu(int tab, u16 item, int pos, u16* out)
{
    s32 cologne;
    int dec;
    SActionMenuItem menu[5];
    *out = 0;
    if (Data.mode == 0) {
        if (item == 0x215) {
            _setExplanationMsgID(0x3C1B);
            menuPocketBattleDiskMain(-1, Data.trainer, Data.hero);
            return 1;
        }
        if (item == 0x200) {
            _setExplanationMsgID(0x3AB8);
            cologne = menuPocketCologne(-1);
            item = cologne;
            if (item == 0) {
                return 1;
            }
            *out = cologne;
            UseFunc f = _getUseFunc(item);
            if (f == 0) {
                _infoBeep(0xC374);
                return 0;
            }
            if (f(item, &dec) == 2) {
                if (dec > 0) {
                    _decItem(cologne, 6, -1, dec);
                }
                return 2;
            }
            return 1;
        }
    }
    int count = _makeActMenu(menu, 4, tab, pos);
    _setExplanationMsgID(0x4296);
    int sel = _actionMenuOpen(menu, count);
    if (sel < 0) {
        return 1;
    }
    ActionFunc f = menu[sel].func;
    if (f == 0) {
        return 1;
    }
    switch (f(tab, pos, &dec)) {
    case 0:
        if (dec > 0) {
            _decItem(item, TabTbl[Data.tab].kind, pos, dec);
        }
        Data.lastSlot = pos;
        return 0;
    case 1:
        return 1;
    case 2:
        if (dec > 0) {
            _decItem(item, TabTbl[Data.tab].kind, pos, dec);
        }
        return 2;
    default:
        return 0;
    }
}
#pragma pop
}

namespace {
bool _execGive(int tab, u16 item, int pos)
{
    if (itemDataBiosGetImportant(itemDataBiosGetPtr(item))) {
        msgctrlSetValue(0x2D, item);
        _infoBeep(0xC37A);
        return false;
    }
    Data.lastSlot = pos;
    return true;
}
}

namespace {
int _execSell(int tab, u16 item, int pos)
{
    u8 se;
    int shop = Data.shop;
    int price = _getItemPrice(item);
    if (price <= 0) {
        menuPtPlaySe(0x13);
#ifdef __MWERKS__
        winMsgOpenWithSE(2, menuShopGetMsgID(&se, shop, 0xB, 0x2D, item, -1), 1, 0, se);
#else
        {
            u32 msgId_ = menuShopGetMsgID(&se, shop, 0xB, 0x2D, item, -1);
            winMsgOpenWithSE(2, msgId_, 1, 0, se);
        }
#endif
        winMsgClose(1);
        return 0;
    }
    _setExplanationMsgID(menuShopGetMsgID(&se, shop, 0xD, 0x2D, item, -1));
    int digits;
    if (_getStockMaxFromTagID(tab) > 100) {
        digits = 3;
    } else {
        digits = 2;
    }
    int total;
    int n = _openNumberInputMenu(1, _getItemNumFromMenuPos(tab, pos), digits, total = price / 2);
    if (n < 0) {
        return 0;
    }
    total *= n;
#ifdef __MWERKS__
    winMsgOpenWithSE(2, menuShopGetMsgID(&se, shop, 9, 0x4B, total, -1), 1, 0, se);
#else
    {
        u32 msgId_ = menuShopGetMsgID(&se, shop, 9, 0x4B, total, -1);
        winMsgOpenWithSE(2, msgId_, 1, 0, se);
    }
#endif
    s8 r = menuSubOpenYesNo(0, -1, -1, 0);
    if (r == 1 || r == -1) {
        winMsgClose(1);
        return 0;
    }
    _decItem(item, TabTbl[Data.tab].kind, pos, n);
    GSsndPlay(0x1A, 0, 0);
    heroAddPokedoru(Data.hero, total);
    _infoOnly(menuShopGetMsgID(&se, shop, 0xA, 0x2D, item, 0x4B, total, -1));
    return 1;
}
}

namespace {
int _execDeposit(int tab, u16 item, int pos)
{
    int n;
    if (!_getItemDeposit(item)) {
        msgctrlSetValue(0x2D, item);
        _infoBeep(0xC37B);
        return 0;
    }
    if (TabTbl[Data.tab].kind != 5) {
        _setExplanationMsgID(0x3AAC);
        int digits;
        if (_getStockMaxFromTagID(tab) > 100) {
            digits = 3;
        } else {
            digits = 2;
        }
        n = _openNumberInputMenu(1, _getItemNumFromMenuPos(tab, pos), digits, 0);
    } else {
        n = 1;
    }
    if (n < 0) {
        return 0;
    }
    if (!_depositPC(item, n)) {
        msgctrlSetValue(0x2D, item);
        _infoBeep(0x3AC0);
        return 0;
    }
    _decItem(item, TabTbl[Data.tab].kind, pos, n);
    msgctrlSetValue(0x2D, item);
    msgctrlSetValue(0x2F, n);
    _infoOnly(0xC37C);
    return 1;
}
}

namespace {
void _setActMenuResizeWork(void)
{
    u32 i;
    for (i = 0; i < 7; i++) {
        ResizeEntry* e = &ActMenuResizeWork[i];
        e->y = menuItemBiosGetPtr(0x2C, e->id)->y;
        e->h = menuItemBiosGetPtr(0x2C, e->id)->h;
    }
}
}

namespace {
int _makeActMenu(SActionMenuItem* out, int mode, int tab, int pos)
{
    u8* itemData = itemDataBiosGetPtr(_getItemIDFromMenuPos(tab, pos));
    u8 battleUse;
    if (itemDataBiosGetBattleUseFunc(itemData) != 0) {
        battleUse = 1;
    } else {
        battleUse = 0;
    }
    const SActionMenuItem* src;
    const TabEntry* t = &TabTbl[Data.tab];
    if (Data.mode == 1 || Data.mode == 6) {
        src = t->battle;
    } else if (Data.mode == 0) {
        src = t->field;
    }
    int n = 0;
    while (src[n].func != 0) {
        n++;
    }
    int i;
    SActionMenuItem* dst = out;
    int count = 0;
    const SActionMenuItem* cur = src;
    for (i = 0; i < n + 1; i++, cur++) {
        if ((!(cur->flags & 1) || battleUse) && (!itemDataBiosGetImportant(itemData) || !(cur->flags & 2))) {
            count++;
            dst->msg = cur->msg;
            dst->func = cur->func;
            dst->flags = cur->flags;
            dst->flags = 0;
            dst++;
        }
    }
    return count;
}
}

namespace {
void _infoBeep(u32 msg)
{
    menuPtPlaySe(0x13);
    winMsgOpen(2, msg, 1, 0);
    winMsgClose(1);
}
}

namespace {
void _infoOnly(u32 msg)
{
    winMsgOpen(2, msg, 1, 0);
    winMsgClose(1);
}
}

extern "C" {
u8 fn_80022128(tagWINDOW_WORK* w)
{
    CursorPos pos = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    if (_getItemIDFromMenuPos(w->cursor, pos.x + pos.y) == 0 && Data.exchangeFrom < 0) {
        return 0;
    }
    return 1;
}
}

extern "C" {
u8 fn_8002208C(tagWINDOW_WORK* w)
{
    CursorPos pos = cursorBiosGetPos(TabTbl[w->cursor].cursorIdx);
    if (Data.exchangeFrom >= 0) {
        return 0;
    }
    int idx = pos.x + pos.y;
    if (_getItemIDFromMenuPos(w->cursor, idx) == 0 && idx == 0) {
        return 0;
    }
    return 1;
}
}

extern "C" {
s32 item_ball_sort_number = 12;
}
extern "C" {
s32 item_dougu_sort_number = 128;
}

#include "xd_pocket_hook.inc"
