#pragma once

#include <JSystem/J2D/J2DSetScreen.hxx>
#include <JSystem/J2D/J2DTextBox.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/G2D/BoundPane.hxx>
#include <SMS/G2D/ExPane.hxx>

class TConsoleStr : public JDrama::TViewObj {
public:
    TConsoleStr(const char *);
    virtual ~TConsoleStr();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;

    f32 getWipeCloseTime();
    int processGo(f32);
    int processMiss(int);
    int processReady(int);
    int processScenario(int);
    int processShineGet(int);
    void startAppearGo();
    void startAppearMiss();
    void startAppearReady();
    void startAppearScenario();
    void startAppearShineGet();
    void startCloseWipe(bool);
    void startOpenWipe();
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_0[4];
    J2DSetScreen *mBigTxScreen;
#else
    J2DSetScreen *mBigTxScreen;
#endif
        // 0x0010
    J2DSetScreen *mOpeningDemoScreen;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0014

    u32 _18[4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[16];
    TBoundPane *mBoundPanes_Go[3];
#else
    TBoundPane *mBoundPanes_Go[3];
#endif
        // 0x0028
    JUTPoint mPoints[66];                 // 0x0034
    TBoundPane *mBoundPanes_ShineGet[6];  // 0x0244
    TBoundPane *mBoundPanes_Miss[7];      // 0x025C
    TExPane *mExPanes_Ready[5];           // 0x0278

    // 0x028C | 0 is stage name pane, 1 is scenario pane
    TExPane *mDemoWipeExPanes[2];

    // 0x0294 | 0 is l pane, 1 is r pane
    TExPane *mDemoMaskExPanes[2];

    // 0x029C | 0 is stage name textbox, 1 is scenario textbox
    J2DTextBox *mDemoTextBoxes[2];
    bool mWipeThing;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_12[7];
    u32 *mJPABaseEmitters[3];
#else
    u8 _pc32_12[3];
    u32 *mJPABaseEmitters[3];
#endif
  // 0x02A8 (JPABaseEmitter)
    u32 mAppearState;
    u32 mWipeState;  //?
};