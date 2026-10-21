#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/SPC/SpcTypedBinary.hxx>

class TEventWatcher : public JDrama::TViewObj {
public:
    TEventWatcher(const char *, const char *);
    TEventWatcher(const char *);
    virtual ~TEventWatcher();

    virtual void perform(u32, JDrama::TGraphics *);
    virtual void launchScript(const char *);
#if __SIZEOF_POINTER__ == 8
#else
    TSpcTypedBinary<TEventWatcher> *_10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    TSpcTypedBinary<TEventWatcher> *_14;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[20];
#else
#endif
};

char *sShineViewObjName;

void evStartMontemanFanfare(TSpcTypedInterp<TEventWatcher> *, u32);
void evStartMontemanBGM(TSpcTypedInterp<TEventWatcher> *, u32);
void evIsWaterMelonIsReached(TSpcTypedInterp<TEventWatcher> *, u32);
void evInvalidatePad(TSpcTypedInterp<TEventWatcher> *, u32);
void evOnNeutralMarioKey(TSpcTypedInterp<TEventWatcher> *, u32);
void evWarpFrontToMario(TSpcTypedInterp<TEventWatcher> *, u32);
void evAppear8RedCoinsAndTimer(TSpcTypedInterp<TEventWatcher> *, u32);
void evAppearReadyGo(TSpcTypedInterp<TEventWatcher> *, u32);
void evSetEventForWaterMelon(TSpcTypedInterp<TEventWatcher> *, u32);