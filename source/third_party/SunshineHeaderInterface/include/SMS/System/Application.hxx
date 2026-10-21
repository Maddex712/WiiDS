#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JGadget/Vector.hxx>
#include <JSystem/JUtility/JUTConsole.hxx>

#include <SMS/GC2D/SMSFader.hxx>
#include <SMS/Player/Mario.hxx>
#include <SMS/System/GameSequence.hxx>
#include <SMS/System/MarDirector.hxx>
#include <SMS/System/MarNameRefGen.hxx>

class TApplication {
public:
    enum Context {
        CONTEXT_DIRECT_LOAD_LOOP,
        CONTEXT_DIRECT_MAIN_LOOP,
        CONTEXT_GAME_BOOT,
        CONTEXT_GAME_BOOT_LOGO,
        CONTEXT_GAME_INTRO,
        CONTEXT_DIRECT_STAGE,
        CONTEXT_DIRECT_MOVIE,
        CONTEXT_GAME_SHUTDOWN,
        CONTEXT_DIRECT_SHINE_SELECT,
        CONTEXT_DIRECT_LEVEL_SELECT
    };

public:
    TApplication();

    bool checkAdditionalMovie();
    void crTimeAry();
    void drawDVDErr();
    void finalize();
    int gameLoop();
    void initialize();
    void initialize_bootAfter();
    void initialize_nlogoAfter();
    void mountStageArchive();
    void proc();
    void setupThreadFuncLogo();

    TApplication *mApplication;                                             // 0x0000
    JDrama::TDirector *mDirector;                                           // 0x0004
    u8 mContext;                                                            // 0x0008
    TGameSequence mPrevScene;                                               // 0x000A
    TGameSequence mCurrentScene;                                            // 0x000E
    TGameSequence mNextScene;                                               // 0x0012
    u32 mCutSceneID;                                                        // 0x0018
    JDrama::TDisplay *mDisplay;                                             // 0x001C
    TMarioGamePad *mGamePads[4];                                            // 0x0020
    TNameRefPtrAryT<TNameRefAryT<TScenarioArchiveName>> *mStageArchiveAry;  // 0x0030
    TSMSFader *mFader;                                                      // 0x0034
    u8 mCurrentSaveBlock;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_13[7];
    JKRHeap *mCurrentHeap;
#else
    JKRHeap *mCurrentHeap;
#endif
  // 0x0040
    u16 _44;

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[6];
#else
    u8 _pc32_tail[2];
#endif
};

void SetupThreadFuncLogo(void *);
void SetupThreadFuncBoot(void *);

void *SMSLoadArchive(const char *path, void *, u32, JKRHeap *heap);
f32 SMSGetAnmFrameRate();
f32 SMSGetVSyncTimesPerSec();

extern TApplication gpApplication;

extern JUTConsole *sReportConsole;
extern JUTConsole *sWarningConsole;
extern JUTResFont *gpSystemFont;
extern JUTResFont *gpRomFont;

extern void *arcBufNLogo;
extern void *arcBufMario;
extern void *arcBufCmn;
extern void *bufStageArcBin;
extern void *spGameHeapBlock;
extern void *gArbkConsole;
extern void *gArbkGuide;

extern OSThread gSetupThread;
extern char *gpSetupThreadStack;