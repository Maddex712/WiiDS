#pragma once

#include <Dolphin/types.h>
#include <SMS/Manager/EnemyManager.hxx>
#include <SMS/Enemy/SpineEnemy.hxx>
#include <SMS/System/Params.hxx>

struct TSmallEnemyParams : TSpineEnemyParams {

    TSmallEnemyParams(const char *prm)
        : TSpineEnemyParams(prm), SMS_TPARAM_INIT(mSLJumpForce, 5.0f),
          SMS_TPARAM_INIT(mSLSearchLength, 600.0f), SMS_TPARAM_INIT(mSLSearchHeight, 600.0f),
          SMS_TPARAM_INIT(mSLSearchAware, 150.0f), SMS_TPARAM_INIT(mSLSearchAngle, 140.0f),
          SMS_TPARAM_INIT(mSLGiveUpLength, 1000.0f), SMS_TPARAM_INIT(mSLGiveUpHeight, 650.0f),
          SMS_TPARAM_INIT(mSLAttackWait, 0), SMS_TPARAM_INIT(mSLFreezeWait, 50),
          SMS_TPARAM_INIT(mSLDamageRadius, 115), SMS_TPARAM_INIT(mSLDamageHeight, 100),
          SMS_TPARAM_INIT(mSLAttackRadius, 90), SMS_TPARAM_INIT(mSLAttackHeight, 60),
          SMS_TPARAM_INIT(mSLTurnSpeedLow, 0.9f), SMS_TPARAM_INIT(mSLTurnSpeedHigh, 1.0f),
          SMS_TPARAM_INIT(mSLBodyScaleLow, 1.0f), SMS_TPARAM_INIT(mSLBodyScaleHigh, 1.6f),
          SMS_TPARAM_INIT(mSLGenItemRate, 10.0f), SMS_TPARAM_INIT(mSLGenEggRate, 50.0f),
          SMS_TPARAM_INIT(mSLPolluteRange, 5), SMS_TPARAM_INIT(mSLWaitTime, 100),
          SMS_TPARAM_INIT(mSLPolluteRMin, 1), SMS_TPARAM_INIT(mSLPolluteRMax, 3),
          SMS_TPARAM_INIT(mSLPolluteCycle, 300), SMS_TPARAM_INIT(mSLStampRange, 5),
          SMS_TPARAM_INIT(mSLPolluteInterval, 60), SMS_TPARAM_INIT(mSLGenerateOnlyDead, 0) {
        load(prm);
    }

    TParamRT<f32> mSLJumpForce;
    TParamRT<f32> mSLSearchLength;
    TParamRT<f32> mSLSearchHeight;
    TParamRT<f32> mSLSearchAware;
    TParamRT<f32> mSLSearchAngle;
    TParamRT<f32> mSLGiveUpLength;
    TParamRT<f32> mSLGiveUpHeight;
    TParamRT<u32> mSLAttackWait;
    TParamRT<u32> mSLFreezeWait;
    TParamRT<u32> mSLDamageRadius;
    TParamRT<u32> mSLDamageHeight;
    TParamRT<u32> mSLAttackRadius;
    TParamRT<u32> mSLAttackHeight;
    TParamRT<f32> mSLTurnSpeedLow;
    TParamRT<f32> mSLTurnSpeedHigh;
    TParamRT<f32> mSLBodyScaleLow;
    TParamRT<f32> mSLBodyScaleHigh;
    TParamRT<f32> mSLGenItemRate;
    TParamRT<f32> mSLGenEggRate;
    TParamRT<u8> mSLPolluteRange;
    TParamRT<u32> mSLWaitTime;
    TParamRT<u32> mSLPolluteRMin;
    TParamRT<u32> mSLPolluteRMax;
    TParamRT<u32> mSLPolluteCycle;
    TParamRT<u8> mSLStampRange;
    TParamRT<u32> mSLPolluteInterval;
    TParamRT<u8> mSLGenerateOnlyDead;
};

class TSmallEnemy : public TSpineEnemy {

public:
    TSmallEnemy(const char *name);
    ~TSmallEnemy();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *, u32) override;
    virtual void init(TLiveManager *) override;
    virtual void reset() override;
    virtual void moveObject() override;
    virtual void kill() override;
    virtual void updateAnmSound() override;
    virtual void loadAfter() override;

    virtual void genRandomItem();
    virtual void genEventCoin();
    virtual void generateItem();
    virtual bool isEatenByYosshi();
    virtual void setBehavior();
    virtual void jumpBehavior();
    virtual void behaveToWater(THitActor *);
    virtual bool changeByJuice();  // unknown4
    virtual u8 changeMove();      // uint
    virtual u32 getChangeBlockTime();
    virtual void scalingChangeActor();
    virtual void changeOut();
    virtual void behaveToTaken(THitActor *);
    virtual void behaveToRelease();
    virtual void setGenerateAnm();
    virtual void setWalkAnm();
    virtual void setDeadAnm();
    virtual void setFreezeAnm();
    virtual void setMeltAnm();
    virtual void setWaitAnm();
    virtual void setRunAnm();
    virtual void attackToMario();
    virtual void forceKill();
    virtual void setMActorAndKeeper();
    virtual void initAttacker(THitActor *);
    virtual bool isHitValid(u32);  //(ulong)
    virtual bool isCollidMove(THitActor *);
    virtual int isInhibitedForceMove();
    virtual void endHitWaterJump();
    virtual void sendAttackMsgToMario();
    virtual void decHpByWater(THitActor *);
    virtual void setBckAnm(int);
    virtual void setDeadEffect();
    virtual void setAfterDeadEffect();
    virtual bool doKeepDistance();
    virtual void generateEffectColumWater();
    virtual bool isFindMario(f32);
    
    void behaveToHitOthers(THitActor *);
    void expandCollision();
    u32 isEaten();  // 1 or 0
    u32 isFindMarioFromParam(f32) const;
    u32 isHitWallInBound();
    u32 isMarioInWater() const;
#if __SIZEOF_POINTER__ == 8
#else
    
   /* const bool mIsAmpPolluter;
    const bool mIsPolluter;*/

    u32 _150;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _154;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _158;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _15C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
 // int
    u32 _160;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _164;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _165;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _166;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _167;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    f32 _168;
#endif

#if __SIZEOF_POINTER__ == 8
#else
 // code address
    f32 _16C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
 // code address
    f32 _170;
#endif

#if __SIZEOF_POINTER__ == 8
#else
 // code address
    u32 _174;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _178;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_14[52];
    u32 _17C;
#else
    u32 _17C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _180;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _184;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _185;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _186;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _187;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    f32 _188;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_21[20];
 // code address
    u32 _18C;
#else
 // code address
    u32 _18C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _190;
#endif
 // code address, seems to be called for setting an anm index?

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};

class TSmallEnemyManager : public TEnemyManager {
public:
    TSmallEnemyManager(const char *);
    virtual ~TSmallEnemyManager();

    virtual void createEnemies(int) override;
    virtual void load(JSUMemoryInputStream &) override;
    virtual void createModelData() override;
    virtual void loadAfter() override;

    virtual void initSetEnemies();

    int getHolder(int);
#if __SIZEOF_POINTER__ == 8
#else
    /*const f32 mBlockMoveSpeed;
    const f32 mBlockWaitMoveY;
    const u32 mBlockWaitTime;
    const f32 mBlockXZScale;
    const f32 mBlockYScale;
    const u32 mChangeBlockTime;
    const u8 mTestJuiceType;*/

    u32 _54;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // code executed after adding enemies
    u32 _58;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // something texStamp Replated
    u32 _5C;
#endif
  // 60

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
#endif
};