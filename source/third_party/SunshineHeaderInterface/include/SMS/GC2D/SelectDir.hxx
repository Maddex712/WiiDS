#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRDirector.hxx>
#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JDrama/JDRScreen.hxx>
#include <JSystem/JKernel/JKRMemArchive.hxx>
#include <JSystem/JParticle/JPAResourceManager.hxx>
#include <SMS/Player/Mario.hxx>
#include <SMS/Player/MarioGamePad.hxx>
#include <SMS/GC2D/SelectMenu.hxx>
#include <SMS/GC2D/SelectGrad.hxx>
#include <SMS/Manager/SelectShineManager.hxx>

class TSelectDir : public JDrama::TDirector {
public:
    TSelectDir();
    virtual ~TSelectDir();

    s32 direct() override;

    void changeOrder();
    void rsetup();
    s32 setup(JDrama::TDisplay *display, TMarioGamePad *controller, u8 areaID);
    void setupThreadFunc(void *);

    TMarioGamePad *mController;
    JDrama::TDisplay *mDisplay;
    TSelectMenu *mMenu;
    TSelectGrad *mGradient;
    TSelectShineManager *mManager;
    JKRMemArchive *mArchive;
    void *mEmitterManager1;  // JPAEmitterManager *
    void *mEmitterManager2;  // JPAEmitterManager *
    bool mIsInitialized;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_10[15];
    u8 mAreaID;
#else
    u8 mAreaID;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_11[7];
    JDrama::TScreen *mScreen1;
#else
    JDrama::TScreen *mScreen1;
#endif

    JDrama::TScreen *mScreen2;
    bool mIsResetting;
};