// Called only inside HeroMove's original field-menu permission block, after script/event/lock checks.
#include "xd_companion.h"
#include "xd_chain.h"
extern "C" void WiidsXD_ReadWorld(XdSnapshot*);
extern "C" int WiidsXD_FieldPdaAllowed(void);
extern "C" int8_t fadeCheck(bool);
extern "C" int WiidsXD_FieldGate() {
    XdSnapshot frame={};WiidsXD_ReadWorld(&frame);
    for(auto& move:frame.moves)move.effectiveness_num=move.effectiveness_den=-1;
    if(frame.battle)return 0;
    if(GSflagGet(0x30d)!=0)return 0; // native field menu 0x70 has different capabilities
    // menuFieldMain itself refuses to open during a fade. Invalidate the old
    // field capability before taking a request, so a transition cannot consume
    // a tap and leave a chain waiting for a menu that was never opened.
    if(fadeCheck(false)!=0){WiidsXD_Publish(&frame,0x58444649454c44ULL,0);return 0;}
    frame.available=1;frame.field_menu=1;frame.phase=2;
    for(int action=XD_BAG;action<=XD_PDA;action++) {
        if(action==XD_PDA&&!WiidsXD_FieldPdaAllowed())continue;
        auto& choice=frame.choices[frame.choice_count++];choice.action=action;choice.enabled=1;choice.cursor=-1;choice.item_id=-1;
    }
    for(int i=0;i<6;i++)if(frame.party[i].species) {
        auto& choice=frame.choices[frame.choice_count++];choice.action=XD_PARTY_MEMBER;choice.argument=i;choice.enabled=1;choice.cursor=-1;choice.item_id=-1;
        for(int n=0;n<XD_TEXT-1&&frame.party[i].name[n];++n)choice.label[n]=frame.party[i].name[n];
    }
    bool physical=GSinputButtonsPressed(1)!=0;
    if(physical)WiidsXD_ChainCancel();
    WiidsXD_Publish(&frame,0x58444649454c44ULL,physical);
    XdRequest request={};
    if(physical||!WiidsXD_Take(&request))return 0;
    WiidsXD_ChainBegin(request.action,request.argument,0);
    return 1; // original code opens menuFieldMain(0), then its own callbacks consume the chain
}
