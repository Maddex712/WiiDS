#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRActor.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <SMS/Enemy/SpineBase.hxx>
#include <SMS/Enemy/SpineEnemy.hxx>
#include <SMS/Manager/EnemyManager.hxx>
#include <SMS/Strategic/HitActor.hxx>

struct TBossPakkunParams : public TSpineEnemyParams {
    TBossPakkunParams(const char *prm)
        : TSpineEnemyParams(prm), SMS_TPARAM_INIT(mSLWaitFrameStg0, 400),
          SMS_TPARAM_INIT(mSLWaterMarkLimit, 600), SMS_TPARAM_INIT(mSLSwingLength, 600.0f),
          SMS_TPARAM_INIT(mSLPollBallStampScale, 1.0f), SMS_TPARAM_INIT(mSLTumbleTime, 0x960),
          SMS_TPARAM_INIT(mSLAnmBlendTime0, 0x3c), SMS_TPARAM_INIT(mSLFlySpeed, 5.0f),
          SMS_TPARAM_INIT(mSLPivotSpeed, 0.7f), SMS_TPARAM_INIT(mSLPivotSpeedAware, 1.5f),
          SMS_TPARAM_INIT(mSLVomitAnmRate, 0.6f), SMS_TPARAM_INIT(mSLHeadHomingLimit, 30.0f),
          SMS_TPARAM_INIT(mSLDamageAngle, 180.0f), SMS_TPARAM_INIT(mSLTornadoProp, 0.4f),
          SMS_TPARAM_INIT(mSLTornadoSpeed, 2.0f), SMS_TPARAM_INIT(mSLTornadoRollSpeed, 0.06f),
          SMS_TPARAM_INIT(mSLTornadoMoveInit, 10000.0f), SMS_TPARAM_INIT(mSLTornadoMoveInc, 1.0f),
          SMS_TPARAM_INIT(mSLTornadoMoveLimit, 10720.0f), SMS_TPARAM_INIT(mSLWaterHitTimer, 0x14),
          SMS_TPARAM_INIT(mSLHoverTimer, 0x4b0), SMS_TPARAM_INIT(mSLPollBallRange, 10000.0f),
          SMS_TPARAM_INIT(mSLPollBallSpeed, 20.0f), SMS_TPARAM_INIT(mSLPollBallFront, 1000.0f) {
        load(prm);
    }

    TParamRT<u32> mSLWaitFrameStg0;       // 0xa8
    TParamRT<u32> mSLWaterMarkLimit;      // 0xbc
    TParamRT<f32> mSLSwingLength;         // 0xd0
    TParamRT<f32> mSLPollBallStampScale;  // 0xe4
    TParamRT<u32> mSLTumbleTime;          // 0xf8
    TParamRT<u32> mSLAnmBlendTime0;       // 0x10c
    TParamRT<f32> mSLFlySpeed;            // 0x120
    TParamRT<f32> mSLPivotSpeed;          // 0x134
    TParamRT<f32> mSLPivotSpeedAware;     // 0x148
    TParamRT<f32> mSLVomitAnmRate;        // 0x15c
    TParamRT<f32> mSLHeadHomingLimit;     // 0x170
    TParamRT<f32> mSLDamageAngle;         // 0x184
    TParamRT<f32> mSLTornadoProp;         // 0x198
    TParamRT<f32> mSLTornadoSpeed;        // 0x1ac
    TParamRT<f32> mSLTornadoRollSpeed;    // 0x1c0
    TParamRT<f32> mSLTornadoMoveInit;     // 0x1d4
    TParamRT<f32> mSLTornadoMoveInc;      // 0x1e8
    TParamRT<f32> mSLTornadoMoveLimit;    // 0x1fc
    TParamRT<u32> mSLWaterHitTimer;       // 0x210
    TParamRT<u32> mSLHoverTimer;          // 0x224
    TParamRT<f32> mSLPollBallRange;       // 0x238
    TParamRT<f32> mSLPollBallSpeed;       // 0x24c
    TParamRT<f32> mSLPollBallFront;       // 0x260
};

class TBPHeadHit;
class TBPNavel;
class TBPTornado;
class TBPPolDrop;

class TBossPakkun : public TSpineEnemy {
public:
    enum class BossPakkunState : s8 { NORMAL, VULNERABLE, MOUTH_OPEN, AIRBORNE_VULNERABLE };

    TBossPakkun(const char *);
    virtual ~TBossPakkun();

    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *, u32) override;
    virtual void init(TLiveManager *) override;
    virtual void setGroundCollision() override;
    virtual void kill() override;
    virtual const char **getBasNameTable() const override;

    void changeBck(int);
    void launchPolDrop();
    void gotHipDropDamage();
    void showMessage(u32);
    void rumblePad(int, const JGeometry::TVec3<float> &);
    void checkMarioRiding();
#if __SIZEOF_POINTER__ == 8
#else
    f32 _14C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _00;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[20];
                         // 0x0154
    TBPPolDrop *mPollutionDrop;
#else
                         // 0x0154
    TBPPolDrop *mPollutionDrop;
#endif
      // 0x0158
    void *TBPVomit;                  // 0x015C
    TBPTornado *mTornado;            // 0x0160
    TBPHeadHit *mHeadHit;            // 0x0164
    TBPNavel *mNavel;                // 0x0168
    BossPakkunState mState;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[3];
          // 0x016C
    u32 _03[0xc / 4];
#else
          // 0x016C
    u32 _03[0xc / 4];
#endif
                // 0x0170
    s8 _04;
#if __SIZEOF_POINTER__ == 8
#else
                          // 0x017C
    u32 _05;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                         // 0x0180
    f32 _06;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                         // 0x0184
    u32 _07;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_13[31];
                         // 0x0188
    TWaterEmitInfo *mWaterEmitInfo;
#else
                         // 0x0188
    TWaterEmitInfo *mWaterEmitInfo;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x018C
    s8 _08;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                          // 0x0190
    u32 _0c[0x24 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_16[40];
               // 0x0194 - 0x01B4
    u32 _09;
#else
               // 0x0194 - 0x01B4
    u32 _09;
#endif
                         // 0x01B8
    s8 _10;
#if __SIZEOF_POINTER__ == 8
#else
                          // 0x01BC
    u32 _11;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                         // 0x01C0
    s8 _12;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_20[8];
                          // 0x01C4
    s8 _12_padding;
#else
                          // 0x01C4
    s8 _12_padding;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_21[2];
                  // 0x01C8 - Padding?
    u32 _13;
#else
                  // 0x01C8 - Padding?
    u32 _13;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                         // 0x01CC
    s8 _14;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
#endif
};

class TNerveBPWait : public TNerveBase<TLiveActor> {
public:
    TNerveBPWait(){};
    virtual ~TNerveBPWait();
    virtual int execute(TSpineBase<TLiveActor> *mSpineBase) const;
    static TNerveBase<TLiveActor> *theNerve();
};
class TNerveBPTakeOff : public TNerveBase<TLiveActor> {
public:
    TNerveBPTakeOff(){};
    virtual ~TNerveBPTakeOff();
    virtual int execute(TSpineBase<TLiveActor> *mSpineBase) const;
    static TNerveBase<TLiveActor> *theNerve();
};

class TNerveBPFly : public TNerveBase<TLiveActor> {
public:
    TNerveBPFly(){};
    TNerveBPFly(const TNerveBPFly &a){};
    virtual ~TNerveBPFly();
    virtual int execute(TSpineBase<TLiveActor> *mSpineBase) const;
    static TNerveBase<TLiveActor> *theNerve();
};

class TNerveBPHover : public TNerveBase<TLiveActor> {
public:
    TNerveBPHover(){};
    virtual ~TNerveBPHover();
    virtual int execute(TSpineBase<TLiveActor> *mSpineBase) const;
    static TNerveBase<TLiveActor> *theNerve();
};

class TBPHeadHit : public THitActor {
public:
    TBPHeadHit(TBossPakkun *parentPakkun, const char *name) : THitActor(name) {
        mParent = parentPakkun;
        initHitActor(0x8000010, 5, -0x7f000000, 300.0f, 500.0f, 300.0f, 500.0f);
        mObjectType = mObjectType & 0xfffffffe;
    }
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *sender, u32 msg) override;

    TBossPakkun *mParent;  // 0x0068
};

class TBPNavel : public THitActor {
public:
    TBPNavel(TBossPakkun *parentPakkun, const char *name) : THitActor(name) {
        mParent = parentPakkun;
        initHitActor(0x8000011, 1, -0x80000000, 200.0f, 300.0f, 200.0f, 300.0f);
    }
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *sender, u32 msg) override;

    TBossPakkun *mParent;  // 0x0068
};

class TBPPolDrop : public THitActor {
public:
    TBPPolDrop(TBossPakkun *parentPakkun, char *name);
    virtual void perform(u32, JDrama::TGraphics *) override;
    void move();

    TBossPakkun *mParent;             // 0x0068
    JGeometry::TVec3<f32> mVelocity;  // 0x006c
    MActor *mActorData;               // 0x0078
    u32 _01[3];
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[4];
                       // 0x007c-84
    f32 _02;
#else
                       // 0x007c-84
    f32 _02;
#endif
                          // 0x0088
};

class TBPTornado : public THitActor {
public:
    TBPTornado(TBossPakkun *parent, const char *);
    virtual ~TBPTornado();
    virtual void perform(u32, JDrama::TGraphics *) override;

    TBossPakkun *mParent;  // 0x68
    MActor *mActorData;    // 0x6C
    TVec3f mTargetPos;
#if __SIZEOF_POINTER__ == 8
#else
     // 0x70
    u32 _74[0x18 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[24];
    f32 _94;
#else
    f32 _94;
#endif
  // 0x94
    u32 _98;  // 0x98
};

class TBossPakkunManager : public TEnemyManager {
public:
    TBossPakkunManager(const char *, int isDemoMode);
    virtual ~TBossPakkunManager();

    virtual void load(JSUMemoryInputStream &);
    virtual void createModelData();

    u32 isDemoMode;
};