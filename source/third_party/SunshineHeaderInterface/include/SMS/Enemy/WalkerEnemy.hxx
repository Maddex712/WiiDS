#pragma once

#include <Dolphin/types.h>
#include <SMS/Enemy/SmallEnemy.hxx>
#include <SMS/System/Params.hxx>

struct TWalkerEnemyParams : TSmallEnemyParams {

    TWalkerEnemyParams(const char *prm)
        : TSmallEnemyParams(prm), SMS_TPARAM_INIT(mSLZigzagCycle, 1000.0f),
          SMS_TPARAM_INIT(mSLZigzagAngle, 30.0f), SMS_TPARAM_INIT(mSLMarchSpeedLow, 1.5f),
          SMS_TPARAM_INIT(mSLMarchSpeedHigh, 2.0f) {
        load(prm);
    }

    TParamRT<f32> mSLZigzagCycle;
    TParamRT<f32> mSLZigzagAngle;
    TParamRT<f32> mSLMarchSpeedLow;
    TParamRT<f32> mSLMarchSpeedHigh;
};

class TWalkerEnemy : public TSmallEnemy {
public:
    TWalkerEnemy(const char *);
    virtual ~TWalkerEnemy();

    virtual void attackToMario() override;
    virtual void init(TLiveManager *) override;
    virtual void initAttacker(THitActor *) override;
    virtual void moveObject() override;
    virtual void reset() override;

    virtual void behaveToFindMario();
    virtual void walkBehavior(int, f32);
    virtual bool isResignationAttack();
    virtual void initialGraphNode();

    
    bool isReachedToGoalXZ();
};