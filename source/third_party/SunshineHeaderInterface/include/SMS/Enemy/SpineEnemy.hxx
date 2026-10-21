#pragma once

#include <Dolphin/types.h>
#include <SMS/Graph/GraphTracer.hxx>
#include <SMS/Strategic/LiveActor.hxx>
#include <SMS/System/Params.hxx>

struct TSpineEnemyParams : TParams {

    TSpineEnemyParams(const char *prm)
        : TParams(prm), SMS_TPARAM_INIT(mSLHeadHeight, 120.0f),
          SMS_TPARAM_INIT(mSLBodyRadius, 30.0f), SMS_TPARAM_INIT(mSLWallRadius, 50.0f),
          SMS_TPARAM_INIT(mSLClipRadius, 300.0f), SMS_TPARAM_INIT(mSLFarClip, 10000.0f),
          SMS_TPARAM_INIT(mSLHitPointMax, 1), SMS_TPARAM_INIT(mSLInstanceNum, 100),
          SMS_TPARAM_INIT(mSLActiveEnemyNum, 10) {
        load(prm);
    }

    TParamRT<f32> mSLHeadHeight;
    TParamRT<f32> mSLBodyRadius;
    TParamRT<f32> mSLWallRadius;
    TParamRT<f32> mSLClipRadius;
    TParamRT<f32> mSLFarClip;
    TParamRT<u8> mSLHitPointMax;
    TParamRT<u8> mSLInstanceNum;
    TParamRT<u8> mSLActiveEnemyNum;
};

class TSpineEnemy : public TLiveActor {

public:
    TSpineEnemy(const char *);
    virtual ~TSpineEnemy();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *, u32) override;
    virtual void init(TLiveManager *) override;
    virtual void calcRootMatrix() override;

    virtual void reset();
    virtual void resetToPosition(const TVec3f &);
    virtual void resetSRTV(const TVec3f &, const TVec3f &, const TVec3f &, const TVec3f &);
    virtual TSpineEnemyParams *getSaveParam() const;
    virtual f32 getPhaseShift() const;
    virtual int isReachedToGoal() const;

    void calcEnemyRootMatrix();
    f32 calcMinimumTurnRadius(f32, f32) const;
    f32 calcTurnSpeedToReach(f32, f32) const;
    bool checkCurAnmEnd(int) const;
    void doShortCut();
    f32 getCurAnmFrameNo(int) const;
    void goToDirectedNextGraphNode(const TVec3f &);
    void goToDirLimitedNextGraphNode(f32);
    void goToExclusiveNextGraphNode();
    void goToRandomEscapeGraphNode();
    void goToRandomNextGraphNode();
    void goToShortestNextGraphNode();
    bool isInSight(const TVec3f &, f32, f32, f32) const;
    void jumpToNextGraphNode();
    void setGoalPathFromGraph();
    void turnToCurPathNode(f32);
    void updateSquareToMario();
    void walkToCurPathNode(f32, f32, f32);
    void zigzagToCurPathNode(f32, f32, f32, f32);

    THitActor* mTarget;
    f32 _F8;   // start of node path x?
    f32 _FC;   // start of node path y?
    f32 _100;
#if __SIZEOF_POINTER__ == 8
#else
  // start of node path z?
    u32 _104;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[12];
  // Target node flag?
    f32 _108;
#else
  // Target node flag?
    f32 _108;
#endif
  // Target node x?
    f32 _10C;  // Target node y?
    f32 _110;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[4];
  // Target node z?
    u32 _114;
#else
  // Target node z?
    u32 _114;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_9[8];
    u32 _118;
#else
    u8 _pc32_9[4];
    u32 _118;
#endif

    u32 *_11C;
#if __SIZEOF_POINTER__ == 8
#else
#endif

    TGraphTracer *mGraphTracer;
#if __SIZEOF_POINTER__ == 8
#else
 // _124
    u16 _128;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _12A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    f32 _12C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _130;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _131;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _132;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u8 _133;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    f32 _134;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 *_138;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_22[24];
    u8 mHealth;
#else
    u8 mHealth;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _13C
    u8 _13D;
#endif

#if __SIZEOF_POINTER__ == 8
#else
     // padding?
    u8 _13E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
     // ^^
    u8 _13F;
#endif

#if __SIZEOF_POINTER__ == 8
#else
     // ^^
    f32 _140;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_27[7];
    f32 _144;
#else
    f32 _144;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _148;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _14C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};