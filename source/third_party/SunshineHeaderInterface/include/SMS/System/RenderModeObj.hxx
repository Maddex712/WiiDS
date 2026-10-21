#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRDisplay.hxx>

void SMSGetRederRect_Game();
void SMSSetupGCLogoRenderingInfo(JDrama::TDisplay *);
void SMSSetupGameRenderingInfo(JDrama::TDisplay *, bool);
void SMSSetupMovieRenderingInfo(JDrama::TDisplay *);
void SMSSetupTitleRenderingInfo(JDrama::TDisplay *);
void SMSSetupTitleRenderMode(GXRenderModeObj *);

#define SMSGetRenderRect_Game SMSGetRederRect_Game

extern char SMSAASamplePattern_non[18];
extern char SMSAASamplePattern_aa[18];

extern char SMSVFilter_non[7];
extern char SMSVFilter_flicker[7];