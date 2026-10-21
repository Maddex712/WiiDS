#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRActor.hxx>
#include <JSystem/JDrama/JDRDirector.hxx>
#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JStage/JSGObject.hxx>

#include <SMS/GC2D/Guide.hxx>

#include <SMS/MoveBG/Coin.hxx>
#include <SMS/MoveBG/Shine.hxx>
#include <SMS/Player/Yoshi.hxx>

#include <SMS/GC2D/GCConsole2.hxx>
#include <SMS/GC2D/ShineFader.hxx>
#include <SMS/MoveBG/DemoCannon.hxx>
#include <SMS/NPC/NpcBase.hxx>

#include <SMS/GC2D/GCConsole2.hxx>
#include <SMS/System/PerformList.hxx>

#include <SMS/Player/MarioGamePad.hxx>

class TMarDirector : public JDrama::TDirector {
public:
    enum Status {
        STATE_INTRO_INIT    = 0,
        STATE_INTRO_PLAYING = 1,
        STATE_GAME_STARTING = 2,
        STATE_NORMAL        = 4,
        STATE_PAUSE_MENU    = 5,
        STATE_FREEZE        = 10,
        STATE_SAVE_CARD     = 11
    };

    enum State { WARP_OUT = 2 };

    TMarDirector();
    ~TMarDirector();

    virtual s32 direct();
    virtual JStage::TObject *JSGFindObject(const char *, JStage::TEObject) const;

    void moveStage();
    void updateGameMode();
    void nextStateInitialize(u8);
    void setMario();
    void currentStateFinalize(u8);
    s32 changeState();
    void fireStreamingMovie(u8);
    void fireEndDemoCamera();
    void fireStartDemoCamera(const char *, const TVec3f *, s32, f32, bool, s32 (*)(u32, u32), u32,
                             JDrama::TActor *, JDrama::TFlagT<u16>);
    void setNextStage(u16 stageID, JDrama::TActor *);
    void movement();
    void fireRideYoshi(TYoshi *);
    void fireGetStar(TShine *);
    void fireGetNozzle(u32 *);  // TItemNozzle
    void fireGetBlueCoin(TCoin *);
    void movement_game();
    u32 *findNearestTalkNPC();
    u32 createObjects();  // returns 0
    void setupObjects();
    bool thpInit();
    void loadParticleMario();
    void loadParticle();
    void initLoadParticle();
    void loadResource();
    s32 setup(JDrama::TDisplay *, TMarioGamePad **, u8 areaID, u8 episodeID);

    TMarioGamePad **mGamePads;
    TPerformList *mPerformListGX;
    TPerformList *mPerformListSilhouette;
    TPerformList *mPerformListGXPost;      // 0x0024
    TPerformList *mPerformListMovement;    // 0x0028
    TPerformList *mPerformListCalcAnim;    // 0x002C
    TPerformList *mPerformListUIElements;  // 0x0030
    TPerformList *mPerformListPreDraw;     // 0x0034 ?
    TPerformList *mPerformListUnk1;        // 0x0038
    TPerformList *mPerformListUnk2;        // 0x003C
    TPerformList *mPerformListPostDraw;    // 0x0040 ?
    TPerformList *mPerformListShineMove;   // 0x0044
    TPerformList *mPerformListShineAnim;   // 0x0048
    u16 mGameState;
#if __SIZEOF_POINTER__ == 8
#else
                        // 0x004C
    u16 _02;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                               // 0x004E
    u32 _03[0x14 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_16[22];
                     // 0x0050
    u8 mCurState;
#else
                     // 0x0050
    u8 mCurState;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                          // 0x0064
    u8 _04;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                                // 0x0065
    u16 _05;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                               // 0x0066
    u32 _06[0xC / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_20[23];
                      // 0x0068
    TGCConsole2 *mGCConsole;
#else
                      // 0x0068
    TGCConsole2 *mGCConsole;
#endif
               // 0x0074
    TGuide *mGuide;                        // 0x0078
    u8 mAreaID;                            // 0x007C
    u8 mEpisodeID;
#if __SIZEOF_POINTER__ == 8
#else
                         // 0x007D
    u16 _09;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                               // 0x007E
    u32 _10;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_26[14];
                               // 0x0080
    void *mTalkCursor;
#else
                               // 0x0080
    void *mTalkCursor;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                     // TTalkCursor
    u32 _88[0x18 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_28[48];
    TBaseNPC *mTalkingNPC;
#else
    TBaseNPC *mTalkingNPC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x00A0
    u32 _A4[2];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_30[8];
    u32 *mpNextState;
#else
    u32 *mpNextState;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                     // 0x00AC
    u32 _11;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_32[8];
                              // 0x00B0
    u8 mNextState;
#else
                              // 0x00B0
    u8 mNextState;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_33[7];
                        // 0x00B4
    JKRMemArchive *mCurrentStageArchive;
#else
                        // 0x00B4
    JKRMemArchive *mCurrentStageArchive;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x00B8
    u32 _12[0x18 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_35[40];
                    // 0x00BC
    u32 *mGame6Data;
#else
                    // 0x00BC
    u32 *mGame6Data;
#endif
                      // 0x00D4
    u32 *mAramArchive;                    // 0x00D8
    TShineFader *mShineFader;
#if __SIZEOF_POINTER__ == 8
#else
             // 0x00DC
    u32 _13[0x44 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_39[76];
                    // 0x00E0
    u8 mNextStateA;
#else
                    // 0x00E0
    u8 mNextStateA;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                       // 0x0124 ?
    u32 _14[0x12C / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_41[539];
                   // 0x0128
    TDemoCannon *mCannonObj;
#else
                   // 0x0128
    TDemoCannon *mCannonObj;
#endif

#if __SIZEOF_POINTER__ == 8
#else
              // 0x0254
    u32 _15;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_43[8];
                              // 0x0258
    TShine *mCollectedShine;
#else
                              // 0x0258
    TShine *mCollectedShine;
#endif

#if __SIZEOF_POINTER__ == 8
#else
              // 0x025C
    u32 _260;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _264;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[2];
#else
#endif
};

extern TMarDirector *gpMarDirector;
extern f32 gEffectCoronaScale;
extern float *gpNPCPtrSaveNormal;
