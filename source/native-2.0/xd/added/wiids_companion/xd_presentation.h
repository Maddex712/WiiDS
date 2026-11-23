#pragma once
#include "xd_companion.h"
// Separate, owned presentation contract. Never participates in action admission.
enum XdScreen { XD_SCREEN_UNKNOWN, XD_SCREEN_FIELD, XD_SCREEN_ROOT, XD_SCREEN_PARTY,
    XD_SCREEN_BATTLE, XD_SCREEN_COMMAND, XD_SCREEN_MOVES, XD_SCREEN_TARGET, XD_SCREEN_COMPAT, XD_SCREEN_BAG, XD_SCREEN_SUMMARY, XD_SCREEN_OPTIONS, XD_SCREEN_DETAIL };
struct XdPresentationState {
    XdSnapshot values;
    int32_t kind, parent, row, item, visible, transition, party_slot, party_context;
    int32_t levels[6];
    uint64_t frame, clean_frame;
    int32_t lower_owner;
    int32_t parent_kind;
    int32_t tab, page, counts[128], active_mask, legal_mask;
    uint16_t tabs[5][XD_TEXT], detail[2048], detail_title[XD_TEXT];
    int32_t title_sky;
};
extern "C" {
void WiidsXD_PresentPublish(const XdPresentationState*);
int WiidsXD_PresentReadV38(XdPresentationState*);
int WiidsXD_LowerReady();
void WiidsXD_LowerWindow(void*,int,int);
void WiidsXD_LowerReadySet(int);
int WiidsXD_DrawOwner(int);
int WiidsXD_DrawOwnerGet();
int WiidsXD_TopPolicy();
void WiidsXD_TopPolicySet(int);
void WiidsXD_CleanCaptured(uint32_t);
uint64_t WiidsXD_CleanFrame();
void WiidsXD_PocketPresentation(XdPresentationState*);
void WiidsXD_StatusPresentation(XdPresentationState*);
void WiidsXD_OptionsPublish(int,const char* const*,const char* const*,int,const char*);
void WiidsXD_OptionsClose();
void WiidsXD_PartyPresentation(XdPresentationState*);
}

struct XdTouchRegion {float x,y,w,h;int action,argument,cursor;};
extern "C" {void WiidsXD_TouchRegions(const XdTouchRegion*,int);int WiidsXD_LowerTouch(float,float);int WiidsXD_TouchTake(int,int*,int*,int*);int WiidsXD_FieldColor();}
extern "C" { void WiidsXD_TopProbe(); int WiidsXD_TopLight(); int32_t WiidsXD_TitleFloorGet(); void WiidsXD_TitleFloor(int32_t); }
