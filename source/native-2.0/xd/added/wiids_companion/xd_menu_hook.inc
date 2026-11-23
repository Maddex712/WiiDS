// Included inside the private decomp's menu.cpp, after its own type declarations.
// No copy of a retail struct layout is kept in the public integration module.
#include "xd_companion.h"
#include "xd_chain.h"
#include "xd_prompt.h"
#include "xd_text.h"
extern "C" void WiidsXD_ReadWorld(XdSnapshot*);
extern "C" void WiidsXD_ReadBattleWindow(void*,XdSnapshot*);
extern "C" void WiidsXD_ReadPartyWindow(void*,XdSnapshot*);
extern "C" int WiidsXD_PartyChainAllowed();
extern "C" int WiidsXD_IsSecretMenu(int);
extern "C" void WiidsXD_ReadSecretWindow(void*,XdSnapshot*);
extern "C" uint16_t WiidsXD_SecretInputKey(void*,const XdRequest*);
extern "C" void WiidsXD_ReadPocketWindow(void*,XdSnapshot*);
extern "C" int WiidsXD_PreparePocketInput(void*,const XdRequest*);
extern "C" void WiidsXD_TouchIntent(int);
extern "C" void menuFightButtonSecretKousan(MenuWindow*);
extern "C" void menuTitleButton(MenuWindow*);
extern "C" void menuNintendoWarningButton(MenuWindow*);
extern "C" int WiidsXD_TitleReady();
extern "C" int WiidsXD_WarningReady();
extern "C" const uint16_t* GSmsgGetGSchar(uint32_t);
extern "C" int WiidsXD_NameEntryReady(void*);
extern "C" void WiidsXD_ReadNameWindow(void*,XdSnapshot*);
extern "C" uint16_t WiidsXD_NameInputKey(void*,const XdRequest*);
extern "C" void menuNameEntrySelectButton(MenuWindow*);
extern "C" void menuPokemonStatusButton(MenuWindow*);
extern "C" void WiidsXD_ReadStatusWindow(void*,XdSnapshot*);
extern "C" void menuPdaCursorCtrl(MenuWindow*);
extern "C" void menuPdaDPMListButton(MenuWindow*);
extern "C" void menuPdaDPMButton(MenuWindow*);
extern "C" void menuPdaMemoListButton(MenuWindow*);
extern "C" void menuPdaMemoXDButton(MenuWindow*);
extern "C" void menuPdaMailListButtonXD(MenuWindow*);
extern "C" void menuPdaMailContentsButtonXD(MenuWindow*);
extern "C" unsigned WiidsXD_PdaMaskDpmList(void*),WiidsXD_PdaMaskDpm(void*),WiidsXD_PdaMaskMemoList(void*),WiidsXD_PdaMaskMemo(void*),WiidsXD_PdaMaskMailList(void*),WiidsXD_PdaMaskMail(void*);
static unsigned (*xdPdaMask(const MenuData* data))(void*) {
    if(!data)return nullptr;
    if(data->buttonCallback==&menuPdaDPMListButton)return WiidsXD_PdaMaskDpmList;
    if(data->buttonCallback==&menuPdaDPMButton)return WiidsXD_PdaMaskDpm;
    if(data->buttonCallback==&menuPdaMemoListButton)return WiidsXD_PdaMaskMemoList;
    if(data->buttonCallback==&menuPdaMemoXDButton)return WiidsXD_PdaMaskMemo;
    if(data->buttonCallback==&menuPdaMailListButtonXD)return WiidsXD_PdaMaskMailList;
    if(data->buttonCallback==&menuPdaMailContentsButtonXD)return WiidsXD_PdaMaskMail;
    return nullptr;
}

static void xdChoice(XdSnapshot* out,int action,int argument,int item,int cursor,bool enabled) {
    if(out->choice_count>=XD_MAX_CHOICES)return;
    auto& c=out->choices[out->choice_count++];
    c.action=action;c.argument=argument;c.item_id=item;c.cursor=cursor;c.enabled=enabled;
}
static bool xdAcceptsButton(MenuWindow* window,const MenuData* data) {
    // PDA backdrop/portrait windows are opened without a waiting input owner.
    // They can briefly become the active window while a real page transitions.
    if(window->id==0x77||window->id==0x7f||window->id==0xa8||
       window->id==0x37||window->id==0x38||window->id==0x40||window->id==0x41)return false;
    return data&&(WiidsXD_IsSecretMenu(window->id)||xdPdaMask(data)||data->f2a==1||(data->f2a==4&&(window->id==0x39||window->id==0x4c||window->id==0x2c||window->id==0x2d||
        data->buttonCallback==&menuFightButtonSecretKousan||data->buttonCallback==&menuTitleButton||data->buttonCallback==&menuNintendoWarningButton||data->buttonCallback==&menuNameEntrySelectButton||data->buttonCallback==&menuPokemonStatusButton)));
}
extern "C" int WiidsXD_MenuOwnsInput() {
    auto* window=windowSearchID(windowGetActiveID());
    return window&&window->phase==2&&!window->close&&(xdAcceptsButton(window,menuDataBiosGetPtr(window->id))||WiidsXD_NameEntryReady(window));
}
static void xdMenuFrame(MenuWindow* active) {
    bool physical=winSysWork.key.on!=0||winSysWork2.key.on!=0;
    if(physical){WiidsXD_TouchIntent(-1);WiidsXD_ChainCancel();}
    if(!active)return; // field input publishes only inside the native HeroMove safety gate
    if(WiidsXD_PromptOwnsFrame())return; // actual message/key-wait owns this turn, including its epoch
    XdSnapshot frame={};
    WiidsXD_ReadWorld(&frame);
    for(auto& m:frame.moves)m.effectiveness_num=m.effectiveness_den=-1;
    bool ready=active&&active->phase==2&&!active->close&&!pauseIsPause()&&winSeqCheckMove(active->id)==0;
    if(active){frame.menu_id=active->id;frame.phase=active->phase;}
    frame.available=ready;
    if(ready) {
        const MenuData* data=menuDataBiosGetPtr(active->id);
        if(!data){frame.available=0;WiidsXD_Publish(&frame,(uintptr_t)active,1);return;}
        bool pdaCursor=data->cursorCallback==&menuPdaCursorCtrl;
        bool ordinaryCursor=active->cursorMode==1||active->cursorMode==2||pdaCursor;
        // Unknown custom/script callbacks need their own native legality adapter.
        bool acceptsButton=xdAcceptsButton(active,data);
        int id=data->firstItem,ordinal=0;
        // Terminate corrupted/cyclic metadata rather than hanging the game thread.
        for(int i=0;i<96&&id>=0;i++) {
            const MenuItem* item=menuItemBiosGetPtr(active->id,id);
            if(!item)break;
            if(acceptsButton&&ordinaryCursor&&item->selectable) {
                xdChoice(&frame,XD_SELECT,id,id,ordinal,true);
                if(!WiidsXD_CopyRowLabel(active->id,id,frame.choices[frame.choice_count-1].label,XD_TEXT)&&item->msg)
                    WiidsXD_CopyMessageLabel((uint32_t)(uintptr_t)item->msg,frame.choices[frame.choice_count-1].label,XD_TEXT);
                if(active->id==0x39&&id>=0x110&&id<=0x116&&(id&1)==0)
                    xdChoice(&frame,XD_MOVE,(id-0x110)/2,id,ordinal,true);
            }
            if(item->selectable)ordinal++;
            if(item->last||item->next==id)break;
            id=item->next;
        }
        // All inputs below flow through the existing button/cursor callbacks.
        if(acceptsButton)xdChoice(&frame,XD_CONFIRM,0,-1,-1,true);
        if(acceptsButton&&(active->cursorFlags&8)==0)xdChoice(&frame,XD_CANCEL,0,-1,-1,true);
        if(acceptsButton&&ordinaryCursor&&(active->cursorCount>0||(pdaCursor&&ordinal>0)))for(int action=XD_UP;action<=(pdaCursor?XD_DOWN:XD_RIGHT);action++)
            xdChoice(&frame,action,0,-1,-1,true);
        if(!ordinaryCursor&&(active->id==0x4c||active->id==0x2c||active->id==0x2d))
            for(int action=XD_UP;action<=(active->id==0x2d?XD_DOWN:XD_RIGHT);action++)xdChoice(&frame,action,0,-1,-1,true);
        frame.field_menu=active->id==0x57||active->id==0x58||active->id==0x70;
        WiidsXD_ReadBattleWindow(active,&frame);
        WiidsXD_ReadSecretWindow(active,&frame);
        WiidsXD_ReadPartyWindow(active,&frame);
        WiidsXD_ReadPocketWindow(active,&frame);
        WiidsXD_ReadNameWindow(active,&frame);
        WiidsXD_ReadStatusWindow(active,&frame);
        if(auto maskReader=xdPdaMask(data)) {
            frame.choice_count=0;unsigned mask=maskReader(active);
            for(int action=XD_CONFIRM;action<=XD_RIGHT;action++)xdChoice(&frame,action,0,-1,-1,(mask&(1u<<action))!=0);
        }
        if(data->f2a==4&&(data->buttonCallback==&menuTitleButton||data->buttonCallback==&menuNintendoWarningButton)) {
            frame.choice_count=0;
            bool title=data->buttonCallback==&menuTitleButton;
            xdChoice(&frame,XD_CONFIRM,0,-1,-1,title?WiidsXD_TitleReady():WiidsXD_WarningReady());
            const char* label=title?"Start":"Continue";
            for(int n=0;label[n]&&n<XD_TEXT-1;n++)frame.choices[0].label[n]=(uint16_t)label[n];
        }
        // Direct shortcuts are admitted only on a confirmed field menu.
        if(acceptsButton&&(active->id==0x57||active->id==0x58)) {
            int bag=active->id==0x57?2:1;
            if(ordinal>0)xdChoice(&frame,XD_PARTY,0,-1,0,true);
            if(ordinal>bag)xdChoice(&frame,XD_BAG,0,-1,bag,true);
            if(active->id==0x57&&ordinal>1)xdChoice(&frame,XD_PDA,0,-1,1,true);
        }
    }
    // A noninteractive decorative window must not invalidate the message endpoint's
    // token once per frame. It cannot consume a touch itself; old tokens expire.
    if(ready&&frame.choice_count==0){
        XdSnapshot previous{};WiidsXD_Read(&previous);
        if(previous.menu_id!=0xffff0001){frame.available=0;WiidsXD_Publish(&frame,(uintptr_t)active,physical);}
        return;
    }
    WiidsXD_Publish(&frame,(uintptr_t)active,physical);
    XdRequest request={};
    bool manual=ready&&!physical&&WiidsXD_Take(&request);
    if(manual)WiidsXD_ChainCancel();
    bool chained=!manual&&WiidsXD_ChainStep(&frame,physical,&request)!=0;
    if(!ready||physical||(!manual&&!chained))return;
    const XdChoice* chosen=nullptr;
    for(uint32_t i=0;i<frame.choice_count;i++) {
        auto& c=frame.choices[i];
        if(c.action==request.action&&c.argument==request.argument&&c.enabled){chosen=&c;break;}
    }
    if(!chosen)return;
    WiidsXD_TouchIntent(request.action);
    if(!chained&&(((active->id==0x3a||active->id==0x8e)&&(request.action==XD_MOVE||request.action==XD_PARTY_MEMBER||request.action==XD_SNAG))||
        (active->id==0x2c&&request.action==XD_SNAG)||
        (active->id==0x4c&&request.action==XD_PARTY_MEMBER&&WiidsXD_PartyChainAllowed())))
        WiidsXD_ChainBegin(request.action,request.argument,frame.actor,active->id);
    if(!WiidsXD_PreparePocketInput(active,&request)){WiidsXD_ChainCancel();return;}
    if(chosen->cursor>=0) {
        active->cursor.p.base=0;active->cursor.p.offset=chosen->cursor;
        if(active->cursorMode==1||active->cursorMode==2||active->id==0x4c)windowCreateCursorSprite(active);
    }
    u16 key=0;
    if(request.action>=XD_UP&&request.action<=XD_RIGHT)key=(u16)(1<<(request.action-XD_UP));
    else if(request.action==XD_CANCEL)key=0x20;
    else key=0x10;
    if(u16 nameKey=WiidsXD_NameInputKey(active,&request))key=nameKey;
    if(u16 secretKey=WiidsXD_SecretInputKey(active,&request))key=secretKey;
    winSysWork.key.on=key;winSysWork.key.trg=key;winSysWork.key.rep=key;
}
