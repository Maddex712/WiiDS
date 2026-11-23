#include "xd_companion.h"
extern "C" int WiidsXD_OwnedSnagBall(void*);
extern "C" int WiidsXD_CurrentSnagAvailable(void);
static int xdPocketBallPosition(int ball) {
    int count=_getNbItemSlot(1);
    if(count<0||count>256)return -1;
    for(int i=0;i<count;i++)if(_getItemIDFromMenuPos(1,i)==ball&&_getItemNumFromMenuPos(1,i)>0)return i;
    return -1;
}
extern "C" void WiidsXD_ReadPocketWindow(void* raw,XdSnapshot* frame) {
    auto* w=(tagWINDOW_WORK*)raw;
    if(!w||(w->menuID!=0x2c&&w->menuID!=0x2d))return;
    bool stable=w->state==2&&Data.exchangeFrom<0&&Data.tabScroll==0&&Data.listScroll==0&&!winSeqCheckMove(w->menuID);
    if(!stable){for(uint32_t i=0;i<frame->choice_count;i++)frame->choices[i].enabled=0;return;}
    if(Data.mode!=1&&Data.mode!=6)return;
    if(!WiidsXD_CurrentSnagAvailable()||frame->choice_count>=XD_MAX_CHOICES)return;
    int cursor=-1;
    if(w->menuID==0x2c) {
        if(!Data.inList||xdPocketBallPosition(WiidsXD_OwnedSnagBall(Data.trainer))<0)return;
    } else {
        if(Data.tab!=1)return;
        CursorPos pos=cursorBiosGetPos(TabTbl[1].cursorIdx);
        int ball=_getItemIDFromMenuPos(1,pos.x+pos.y);
        if(!ball||_getItemNumFromMenuPos(1,pos.x+pos.y)==0)return;
        auto* menu=(ActionMenuParam*)w->params[0];
        if(!menu||menu->n<1||menu->n>4)return;
        for(int i=0;i<menu->n;i++)if(menu->menu[i].msg==0x4292&&menu->menu[i].func==_actTukauNormal)cursor=i;
        if(cursor<0)return;
    }
    auto& c=frame->choices[frame->choice_count++];c.action=XD_SNAG;c.enabled=1;c.cursor=cursor;c.item_id=-1;
}
extern "C" int WiidsXD_PreparePocketInput(void* raw,const XdRequest* request) {
    auto* w=(tagWINDOW_WORK*)raw;
    if(request->action!=XD_SNAG||!w||w->menuID!=0x2c)return 1;
    if(!WiidsXD_CurrentSnagAvailable()||Data.exchangeFrom>=0||Data.tabScroll!=0||Data.listScroll!=0)return 0;
    int position=xdPocketBallPosition(WiidsXD_OwnedSnagBall(Data.trainer));
    if(position<0)return 0;
    // Select the native tab/row, then let the original A-button path open Use/Cancel.
    w->cursor=1;
    CursorPos pos;pos.y=position<9?position:8;pos.x=position-pos.y;
    cursorBiosSetPos(TabTbl[1].cursorIdx,pos);
    return 1;
}
