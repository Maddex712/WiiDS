#include "xd_presentation.h"
extern "C" s32 GSfloorGetCurrentFloorGroupID(void);
extern "C" u8 menuFieldIsOpen();
extern "C" int heroMoveGetLeader();
extern "C" bool heroMoveIsMember(int);
extern "C" void heroMoveGetResID(u32*,u32*,int);
extern "C" u8 peopleCheckFlag(u32,u32,u32);
extern "C" s32 menuGetCursorItemID(s32);
extern "C" int WiidsXD_SettingsEntry(int);
static XdPresentationState xdVisual{};
static uint64_t xdVisualFrame=0;
static bool xdRootId(int id){return id==0x57||id==0x58||id==0x70;}
static bool xdPartyId(int id){return id==0x4c||id==0x46||id==0x47||id==0x48||id==0x49||id==0x4a||id==0x4b||id==0x4d;}
extern "C" void WiidsXD_ReadCurrentBattle(XdSnapshot*);
extern "C" void WiidsXD_ReadBattleWindow(void*,XdSnapshot*);
static bool xdSummaryId(int id){return id==0x5e||id==0x5f||id==0x60||id==0x62||id==0x63;}
static bool xdBagId(int id){return id==0x2c||id==0x2d;}
static bool xdBattleMenu(int id){return id==0x3a||id==0x39||id==0x5c;}
static bool xdBattleSlice(){return xdVisual.values.battle&&
    (xdVisual.kind==XD_SCREEN_COMMAND||xdVisual.kind==XD_SCREEN_MOVES||xdVisual.kind==XD_SCREEN_TARGET);}
static bool xdBattleSprite(int window,int sprite){
    // Explicit ordinary-menu ownership. Question text AND its three-part
    // native backing stay above; HP/dialogue/status windows never enter here.
    if(window==0x3a)return (sprite>=0x11f&&sprite<=0x127)||(sprite>=0x12b&&sprite<=0x136);
    if(window==0x39)return (sprite>=0xf9&&sprite<=0x107)||(sprite>=0x10b&&sprite<=0x11c)||sprite==0x11e;
    return window==0x5c&&sprite>=0x138&&sprite<=0x13b;
}
static int xdBattleLabel(int window,int item){
    if(window==0x3a)switch(item){case 0x12c:return 0x133;case 0x12e:return 0x135;case 0x130:return 0x134;case 0x132:return 0x136;}
    return item;
}
static bool xdShown(const MenuWindow* w){return w&&(w->flags&2)&&w->phase<4;}
// Whole-window callbacks remain field-only; battle is split per sprite.
static bool xdLowerWindow(const MenuWindow* w){
    if(!xdVisual.lower_owner||!w)return false;
    return (xdVisual.kind==XD_SCREEN_ROOT&&xdRootId(w->id))||
           (xdVisual.kind==XD_SCREEN_PARTY&&(xdPartyId(w->id)||xdRootId(w->id)))||
           (xdVisual.kind==XD_SCREEN_BAG&&(xdBagId(w->id)||xdRootId(w->id)||xdBattleMenu(w->id)));
}
static bool xdLowerSprite(const MenuWindow* w,const MenuSprite* s){
    if(!w||!s)return false;
    if(xdVisual.lower_owner&&xdVisual.kind==XD_SCREEN_SUMMARY){
        if(xdPartyId(w->id)||xdRootId(w->id))return true;
        if(w->id==0x5e){int i=s->id;
            // Native identity/stats/help stay above; model and move/list/navigation belong below.
            return i==0x81f||(i>=0x80c&&i<=0x812)||i==0x815||i==0x81c||
              (i>=0x1fd&&i<=0x205)||(i>=0x24f&&i<=0x258)||(i>=0x388&&i<=0x38b);
        }
    }
    if(xdVisual.lower_owner&&xdVisual.kind==XD_SCREEN_BAG&&xdLowerWindow(w))return true;
    if(xdVisual.lower_owner&&xdBattleSlice())return xdBattleSprite(w->id,s->id);
    // Field ownership still requires a native item/cursor in its owned window.
    if(!xdLowerWindow(w))return false;
    const MenuData* d=menuDataBiosGetPtr(w->id);
    if(!d)return false;
    int id=d->firstItem;
    for(int n=0;n<128&&id>=0;n++){
        auto* item=menuItemBiosGetPtr(w->id,id);if(!item)break;
        if(s->id==id||s->id==item->cursorSprite)return true;
        if(item->last||item->next==id)break;id=item->next;
    }
    // Runtime cursor sprites carry their owning selectable item id as well.
    return false;
}
struct XdDrawScope {
    int old;
    explicit XdDrawScope(bool lower):old(WiidsXD_DrawOwner(lower?1:0)){}
    ~XdDrawScope(){WiidsXD_DrawOwner(old);}
};
static MenuWindow* xdVisualWindow(){
    auto* summary=windowSearchID(0x5e);if(xdShown(summary))return summary;
    auto* active=windowSearchID(windowGetActiveID());
    if(xdShown(active)&&!xdPartyId(active->id)&&!xdRootId(active->id))return active;
    auto* party=windowSearchID(0x4c);
    if(xdShown(party))return party;
    if(xdShown(active))return active;
    for(auto* w=winSysWork.head;w;w=w->next)if(xdShown(w)&&xdRootId(w->id))return w;
    return nullptr;
}
static void xdPresentRows(MenuWindow* w,bool afterDrawing=false){
    auto& s=xdVisual.values;s.choice_count=0;
    if(!w)return;
    const auto* d=menuDataBiosGetPtr(w->id);if(!d)return;
    int id=d->firstItem,ordinal=0;
    for(int n=0;n<128&&id>=0;n++){
        const auto* item=menuItemBiosGetPtr(w->id,id);if(!item)break;
        // Row existence is independent of phase/input readiness. Native metadata
        // contains decorations too; only controller rows enter the lower list.
        if(item->selectable&&s.choice_count<XD_MAX_CHOICES){
            auto& c=s.choices[s.choice_count++];c={};c.action=XD_SELECT;
            c.argument=id;c.item_id=id;c.cursor=ordinal;c.enabled=1;
            int labelId=xdBattleLabel(w->id,id);
            auto* labelItem=menuItemBiosGetPtr(w->id,labelId);
            if(!WiidsXD_CopyRowLabel(w->id,labelId,c.label,XD_TEXT)&&labelItem&&labelItem->msg)
                WiidsXD_CopyMessageLabel((uint32_t)(uintptr_t)labelItem->msg,c.label,XD_TEXT);
            ++ordinal;
        }
        if(item->last||item->next==id)break;id=item->next;
    }
    if(afterDrawing&&s.battle&&xdBattleMenu(w->id)){
        // Original adapter reads native actor/move/target legality (initKeep=0).
        // Called after controller callbacks, independently of input admission.
        WiidsXD_ReadCurrentBattle(&s);
        WiidsXD_ReadBattleWindow(w,&s);
        // Only native SELECT rows are drawn below; shortcut choices are inert.
        uint32_t count=0;
        for(uint32_t i=0;i<s.choice_count;i++)if(s.choices[i].action==XD_SELECT)s.choices[count++]=s.choices[i];
        s.choice_count=count;
        if(w->id==0x39)for(uint32_t i=0;i<count;i++){
            auto& c=s.choices[i];int slot=(c.item_id-0x110)/2;
            if(c.item_id>=0x110&&c.item_id<=0x116&&!(c.item_id&1)){
                c.enabled=s.moves[slot].legal;
                if(s.moves[slot].name[0])for(int n=0;n<XD_TEXT;n++)c.label[n]=s.moves[slot].name[n];
            }
        }
    }
    if(xdVisual.kind==XD_SCREEN_ROOT&&s.choice_count<XD_MAX_CHOICES){
        int settings=WiidsXD_SettingsEntry(w->id);
        if(settings>=0){
            auto& c=s.choices[s.choice_count++];c={};c.action=XD_SELECT;c.argument=c.item_id=-2;c.cursor=ordinal;c.enabled=1;
            const char* label="Settings";for(int i=0;label[i];i++)c.label[i]=label[i];
            if(settings){xdVisual.row=ordinal;xdVisual.item=-2;}
        }
    }
}
static void xdPresentBuild(){
    xdVisual={};auto& s=xdVisual.values;
    WiidsXD_ReadWorld(&s);
    auto* w=xdVisualWindow();
    xdVisual.visible=1;xdVisual.row=-1;xdVisual.item=-1;xdVisual.party_slot=-1;
    xdVisual.frame=++xdVisualFrame;xdVisual.clean_frame=WiidsXD_CleanFrame();
    s.version=3;s.sequence=xdVisual.frame;s.captured_ms=WiidsXD_Now();
    if(w){
        s.menu_id=w->id;s.phase=w->phase;
        xdVisual.parent=w->parent?w->parent->id:0;
        xdVisual.row=w->cursor.p.base+w->cursor.p.offset;
        if(xdVisual.row<0||xdVisual.row>=128)xdVisual.row=-1;
        xdVisual.item=menuGetCursorItemID(w->id);
        xdVisual.transition=w->close||w->phase>=3?2:w->phase<2?1:0;
        s.available=w->phase==2&&!w->close&&!pauseIsPause()&&!winSeqCheckMove(w->id);
        xdVisual.kind=XD_SCREEN_COMPAT;
        if(!s.battle&&menuFieldIsOpen()){
            if(xdRootId(w->id))xdVisual.kind=XD_SCREEN_ROOT;
            else if(xdPartyId(w->id))xdVisual.kind=XD_SCREEN_PARTY;
        }
        // Ordinary battle only; alternative services retain native upper UI.
        if(s.battle){
            xdVisual.kind=w->id==0x3a?XD_SCREEN_COMMAND:
                w->id==0x39?XD_SCREEN_MOVES:w->id==0x5c?XD_SCREEN_TARGET:XD_SCREEN_COMPAT;
        }
        if(xdPartyId(w->id))xdVisual.kind=XD_SCREEN_PARTY;
        if(xdBagId(w->id))xdVisual.kind=XD_SCREEN_BAG;
        if(xdSummaryId(w->id))xdVisual.kind=XD_SCREEN_SUMMARY;
    }else{
        xdVisual.kind=s.battle?XD_SCREEN_BATTLE:menuFieldIsOpen()?XD_SCREEN_ROOT:
            s.map_id&&s.map_name[0]?XD_SCREEN_FIELD:XD_SCREEN_UNKNOWN;
        xdVisual.transition=menuFieldIsOpen()?1:0;
    }
    s.field_menu=xdVisual.kind==XD_SCREEN_ROOT;
    xdVisual.parent_kind=xdVisual.kind==XD_SCREEN_ROOT?XD_SCREEN_FIELD:
        xdVisual.kind==XD_SCREEN_PARTY?XD_SCREEN_ROOT:
        xdVisual.kind==XD_SCREEN_COMMAND?XD_SCREEN_BATTLE:
        xdVisual.kind==XD_SCREEN_MOVES?XD_SCREEN_COMMAND:
        xdVisual.kind==XD_SCREEN_TARGET?XD_SCREEN_MOVES:XD_SCREEN_UNKNOWN;
    xdPresentRows(w);
    if(xdVisual.kind==XD_SCREEN_PARTY)WiidsXD_PartyPresentation(&xdVisual);
    if(xdVisual.kind==XD_SCREEN_BAG)WiidsXD_PocketPresentation(&xdVisual);
    if(xdVisual.kind==XD_SCREEN_SUMMARY)WiidsXD_StatusPresentation(&xdVisual);
    xdVisual.lower_owner=WiidsXD_LowerReady()&&
        ((xdVisual.clean_frame&&(xdVisual.kind==XD_SCREEN_ROOT||xdVisual.kind==XD_SCREEN_PARTY))||xdBattleSlice()||xdVisual.kind==XD_SCREEN_BAG||xdVisual.kind==XD_SCREEN_SUMMARY||xdVisual.kind==XD_SCREEN_PARTY);
    const int32_t title=WiidsXD_TitleFloorGet();
    xdVisual.title_sky=title>=0&&GSfloorGetCurrentFloorGroupID()==title&&!s.battle&&!menuFieldIsOpen()&&!xdShown(windowSearchID(0xDB));
    // The native event lock spans speaker swaps and scripted pauses between boxes.
    if(!s.battle&&!xdVisual.title_sky&&xdVisual.kind==XD_SCREEN_FIELD){
        const int leader=heroMoveGetLeader();
        if(heroMoveIsMember(leader)){
            u32 group=0,id=0;heroMoveGetResID(&group,&id,leader);
            if(peopleCheckFlag(group,id,0x80000000))xdVisual.transition=1;
        }
    }
    WiidsXD_TopPolicySet(xdVisual.lower_owner?
        (xdVisual.kind==XD_SCREEN_ROOT||xdVisual.kind==XD_SCREEN_PARTY||xdVisual.kind==XD_SCREEN_BAG)?1:0:0);
}
static void xdPresentFinish(){
    auto* w=windowSearchID((s32)xdVisual.values.menu_id);
    xdPresentRows(w,true); // capture text generated by this frame's original callbacks
    if(xdVisual.kind==XD_SCREEN_PARTY)WiidsXD_PartyPresentation(&xdVisual);
    if(xdVisual.kind==XD_SCREEN_BAG)WiidsXD_PocketPresentation(&xdVisual);
    if(xdVisual.kind==XD_SCREEN_SUMMARY)WiidsXD_StatusPresentation(&xdVisual);
    WiidsXD_PresentPublish(&xdVisual);
}

// Design pass 10c: where the lower title backdrop is copied. XD renders its GS layers in list order
// (title floor: background, menu extra, main, secondary, filter, menu); each window sprite (logo, messages,
// menus) goes to layer 7 or the floor's menu layer, chosen per sprite. layer.cpp calls this with -1 once per
// frame: on the title floor it collects the layers that the shown windows' visible sprites draw into this frame.
// Layers 6 (filter: fades) and 7 (menu) always count, so a fade is never baked in and a title frame with no
// window still gets its copy. It then asks for each layer in order; the first collected one returns 1 and gets
// the copy block in front.
extern "C" s32 winSpriteGetLayerID(void*,void*);
extern "C" bool winSpriteGetDisp(void*);
extern "C" int WiidsXD_BackdropSplit(int layer){
    static uint32_t mask=0;
    if(layer<0){
        mask=0;
        const int32_t title=WiidsXD_TitleFloorGet();
        if(title<0||GSfloorGetCurrentFloorGroupID()!=title||xdShown(windowSearchID(0xDB)))return 0;
        for(auto* w=winSysWork.head;w;w=w->next){
            if(!(w->flags&2))continue;
            MenuSprite* lists[2]={w->sprites,w->overlaySprites};
            for(int k=0;k<2;k++)for(MenuSprite* sp=lists[k];sp;sp=sp->next)
                if(winSpriteGetDisp(sp)){const s32 id=winSpriteGetLayerID(w,sp);if(id>=0&&id<32)mask|=1u<<id;}
        }
        mask|=1u<<6|1u<<7;return 1;
    }
    if(layer>=32||!(mask>>layer&1))return 0;
    mask=0;return 1;
}
