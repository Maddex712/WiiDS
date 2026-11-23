#include "xd_companion.h"
static bool xdNameReady(tagWINDOW_WORK* w){return w&&w->menuID==0x66&&w->state==2&&!w->done&&entryArg(w)!=nullptr;}
extern "C" int WiidsXD_NameEntryReady(void* raw){return xdNameReady((tagWINDOW_WORK*)raw);}
extern "C" void WiidsXD_ReadNameWindow(void* raw,XdSnapshot* frame){
    auto* w=(tagWINDOW_WORK*)raw;if(!xdNameReady(w))return;
    auto* a=entryArg(w);frame->choice_count=0;
    auto add=[&](int action,int arg,bool enabled,const char* label){
        auto& c=frame->choices[frame->choice_count++];c.action=action;c.argument=arg;c.item_id=-1;c.cursor=-1;c.enabled=enabled;
        if(label)for(int i=0;label[i]&&i<XD_TEXT-1;i++)c.label[i]=(uint16_t)label[i];
    };
    add(XD_CONFIRM,0,true,"Choose character");add(XD_CANCEL,0,*a->nameCurX>0,"Backspace");
    add(XD_UP,0,*a->curY>0,nullptr);add(XD_DOWN,0,*a->curY<7,nullptr);
    add(XD_LEFT,0,*a->curX>0,nullptr);add(XD_RIGHT,0,*a->curX<(*a->curY<6?10:7),nullptr);
    add(XD_SELECT,0x58444e30,chkValidName(a->name)!=0||chkValidName(a->defName)!=0,"Done");
    add(XD_SELECT,0x58444e31,true,"Change characters");
}
extern "C" uint16_t WiidsXD_NameInputKey(void* raw,const XdRequest* request){
    if(!xdNameReady((tagWINDOW_WORK*)raw)||request->action!=XD_SELECT)return 0;
    if(request->argument==0x58444e30)return 0x800;
    if(request->argument==0x58444e31)return 0x40;
    return 0;
}
