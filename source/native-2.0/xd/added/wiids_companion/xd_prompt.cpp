#include "xd_prompt.h"
#include "xd_companion.h"
#include "xd_chain.h"
#include <atomic>
static std::atomic<bool> promptOwnsNextMenuFrame{false};
static std::atomic<uintptr_t> promptToken{1};
extern "C" uintptr_t WiidsXD_NewPromptToken(){return promptToken.fetch_add(1);}
extern "C" int WiidsXD_PromptOwnsFrame(){return promptOwnsNextMenuFrame.exchange(false)?1:0;}
extern "C" {
void WiidsXD_ReadWorld(XdSnapshot*);
void WiidsXD_ReadCurrentBattle(XdSnapshot*);
uint32_t GSinputButtonsPressed(int);
int WiidsXD_MenuOwnsInput();
}
extern "C" int WiidsXD_PromptAction(uintptr_t cookie,int allowCancel) {
    if(!allowCancel&&WiidsXD_MenuOwnsInput())return 0; // text behind an interactive menu never steals its touch surface
    promptOwnsNextMenuFrame.store(true);
    XdSnapshot frame={};WiidsXD_ReadWorld(&frame);WiidsXD_ReadCurrentBattle(&frame);
    for(auto& move:frame.moves)move.effectiveness_num=move.effectiveness_den=-1;
    frame.menu_id=0xffff0001;frame.phase=2;frame.available=1;
    frame.choice_count=allowCancel?2:1;
    frame.choices[0].action=XD_CONFIRM;frame.choices[0].enabled=1;frame.choices[0].cursor=-1;
    if(allowCancel){frame.choices[1].action=XD_CANCEL;frame.choices[1].enabled=1;frame.choices[1].cursor=-1;}
    bool physical=GSinputButtonsPressed(1)!=0;
    // A native message/legality prompt is a new decision, never an automatic chain step.
    WiidsXD_ChainCancel();WiidsXD_Publish(&frame,cookie,physical);
    XdRequest request={};
    return !physical&&WiidsXD_Take(&request)?request.action:0;
}
extern "C" int WiidsXD_PromptGate(uintptr_t cookie,int allowCancel){return WiidsXD_PromptAction(cookie,allowCancel)!=0;}
