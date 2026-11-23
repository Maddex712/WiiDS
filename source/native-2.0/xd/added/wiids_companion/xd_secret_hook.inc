// Native controller-private battle menus. Touch enters the original C-stick /
// button callbacks; the original action/result/confirmation flow stays in charge.
static u16 xdSecretStick=0;
extern "C" u16 WiidsXD_SecretStick(){u16 result=xdSecretStick;xdSecretStick=0;return result;}
extern "C" int WiidsXD_IsSecretMenu(int id){return id==0x8e||id==0x90||id==0x91||id==0x92;}
static void xdSecretLabel(XdChoice& choice,const char* text){for(int i=0;i<XD_TEXT-1&&text[i];i++)choice.label[i]=(uint16_t)text[i];}
extern "C" void WiidsXD_ReadSecretWindow(void* raw,XdSnapshot* frame){
    auto* w=(tagWINDOW_WORK*)raw;xdSecretStick=0;
    if(!w||!WiidsXD_IsSecretMenu(w->menuID))return;
    frame->choice_count=0;
    if(w->menuID==0x8e){
        for(int i=0;i<4;i++)xdBattleChoice(frame,XD_MOVE,i,-1,-1,frame->moves[i].legal,frame->moves[i].name);
        for(int i=0;i<6;i++)xdBattleChoice(frame,XD_PARTY_MEMBER,i,-1,-1,WiidsXD_BattlePartyLegal(i),frame->party[i].name);
        bool escape=fightFloorGetStatus(nullptr,0,0x22,0)!=0;
        xdBattleChoice(frame,XD_RUN,0,-1,-1,escape);
        xdSecretLabel(frame->choices[frame->choice_count-1],"Give in");
    }else if(w->menuID==0x90){
        for(int i=0;i<4;i++)xdBattleChoice(frame,XD_MOVE,i,-1,-1,frame->moves[i].legal,frame->moves[i].name);
    }else if(w->menuID==0x92){
        for(int i=0;i<6;i++)xdBattleChoice(frame,XD_PARTY_MEMBER,i,-1,-1,WiidsXD_BattlePartyLegal(i),frame->party[i].name);
    }else if(w->menuID==0x91){
        auto* arg=(TargetArg*)windowGetParam(w,0);
        auto* targets=(TargetSecretEntry*)arg;
        if(targets)for(int i=0;i<4;i++){
            int cursor=targetSecretCursor(targets[i].id);
            if(targets[i].id==0||cursor<0||cursor>3)continue;
            xdBattleChoice(frame,XD_SELECT,cursor,targets[i].id,-1,true);
            auto& choice=frame->choices[frame->choice_count-1];
            const uint16_t* name=(const uint16_t*)arg->entry[i].unk8;
            const char prefix[]="Target ";int at=0;
            for(;prefix[at];at++)choice.label[at]=(uint16_t)prefix[at];
            choice.label[at++]=(uint16_t)('1'+i);choice.label[at++]=':';choice.label[at++]=' ';
            if(name)for(int n=0;name[n]&&at<XD_TEXT-1;n++)choice.label[at++]=name[n];
        }
    }
    bool cancel=w->menuID!=0x92||windowGetParam(w,2)!=0;
    xdBattleChoice(frame,XD_CANCEL,0,-1,-1,cancel);
}
extern "C" u16 WiidsXD_SecretInputKey(void* raw,const XdRequest* request){
    auto* w=(tagWINDOW_WORK*)raw;if(!w||!WiidsXD_IsSecretMenu(w->menuID))return 0;
    if(request->action==XD_CANCEL)return 0x200;
    if(w->menuID==0x8e){
        if(request->action==XD_MOVE)return 0x10;
        if(request->action==XD_PARTY_MEMBER)return 0x20;
        if(request->action==XD_RUN)return 0x800;
    }
    const u16 stick[]={1,8,2,4};
    if((w->menuID==0x90&&request->action==XD_MOVE)||(w->menuID==0x92&&request->action==XD_PARTY_MEMBER)){
        int n=request->argument;if(n>=0&&n<4){xdSecretStick=stick[n];return 0x10;}
        if(w->menuID==0x92&&n==4)return 0x80;
        if(w->menuID==0x92&&n==5)return 0x40;
    }
    if(w->menuID==0x91&&request->action==XD_SELECT){const u16 target[]={1,8,4,2};return target[request->argument&3];}
    return 0;
}
