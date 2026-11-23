#include "xd_companion.h"
#include <game/pxdvs/GSAPI/GSflag/flagData.h>
extern "C" {extern FlagEtc* flag_etc;extern u32* flag_etc_number;const uint16_t* GSmsgGetGSchar(u32);}
// Live disc-backed developer story table: expose its next named milestone.
// These are native story descriptions, not a guessed walkthrough or flags we write.
extern "C" void WiidsXD_ReadObjective(XdSnapshot* out) {
    out->objective_known=0;out->objective[0]=0;
    if(!flag_etc||!flag_etc_number||*flag_etc_number>512)return;
    const FlagEtc* next=nullptr;
    for(u32 i=1;i<*flag_etc_number;i++) {
        const auto& row=flag_etc[i];
        if(row.msg&&row.flagValue>out->story_progress&&(!next||row.flagValue<next->flagValue))next=&row;
    }
    if(!next)return;
    const uint16_t* description=GSmsgGetGSchar(next->msg);if(!description||!description[0])return;
    const char* prefix="Next story milestone: ";int at=0;
    while(*prefix&&at<95)out->objective[at++]=(uint16_t)*prefix++;
    for(int i=0;description[i]&&at<95;i++)out->objective[at++]=description[i];
    out->objective[at]=0;out->objective_known=1;
}
