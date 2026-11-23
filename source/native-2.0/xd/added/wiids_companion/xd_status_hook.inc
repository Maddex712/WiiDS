// Native summary screen adapter; all operations still enter its own handlers.
#include "xd_companion.h"
extern "C" u8 winSeqCheckMove(s32);
extern "C" void WiidsXD_ReadStatusWindow(void* raw,XdSnapshot* frame){
    auto* w=(tagWINDOW_WORK*)raw;
    if(!w||w->menuId!=0x5e)return;
    frame->choice_count=0;
    if(w->state!=2||_menuStatus.state==0||_menuStatus.state==9||winSeqCheckMove(w->menuId))return;
    auto add=[&](int action,bool enabled){auto& c=frame->choices[frame->choice_count++];
        c.action=action;c.cursor=-1;c.item_id=-1;c.enabled=enabled;};
    int state=_menuStatus.state;
    bool confirm=state==2||(state==5&&_menuStatus.ribbon.count>0)||
        (state==3&&_menuStatus.pokemon&&!pokemonGetStatus(_menuStatus.pokemon,0,0xc2,0)&&(_menuStatus.flags&2))||
        (state==4&&_menuStatus.f3!=_menuStatus.cursor)||(state==7&&(_menuStatus.flags&0x10));
    add(XD_CONFIRM,confirm);add(XD_CANCEL,true);
    bool pages=state==1||state==2||state==5||state==8;
    add(XD_LEFT,pages?w->cursor.pos>0:state==6);
    add(XD_RIGHT,pages?w->cursor.pos<3:state==6);
    bool vertical=pages?_menuStatus.cb!=nullptr:(state==3||state==4||state==6||state==7);
    add(XD_UP,vertical);add(XD_DOWN,vertical);
}
