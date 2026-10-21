#pragma once

#include <BetterSMS/player.hxx>
#include <SMS/System/Params.hxx>

struct PlayerMovementParams : public TParams {
    PlayerMovementParams()
        : TParams(), SMS_TPARAM_INIT(mMaxJumps, 1), SMS_TPARAM_INIT(mGravityMultiplier, 1.0f),
          SMS_TPARAM_INIT(mSpeedMultiplier, 1.0f), SMS_TPARAM_INIT(mBaseJumpMultiplier, 1.0f),
          SMS_TPARAM_INIT(mMultiJumpMultiplier, 0.875f),
          SMS_TPARAM_INIT(mMultiJumpFSpeedMulti, 0.9f), SMS_TPARAM_INIT(mSlideMultiplier, 1.0f) {
        load("/mario/better_movement.prm");
    }

    enum class FluddCleanType : u8 { NONE, CLEAN, GOOP };

    TParamRT<u8> mMaxJumps;
    TParamRT<f32> mGravityMultiplier;
    TParamRT<f32> mSpeedMultiplier;
    TParamRT<f32> mBaseJumpMultiplier;
    TParamRT<f32> mMultiJumpMultiplier;
    TParamRT<f32> mMultiJumpFSpeedMulti;
    TParamRT<f32> mSlideMultiplier;
};

struct PlayerMovementData {
    PlayerMovementData() : mCurJump(0), mIsLongJumping(false), mIsHoverBurstValid(true), mLastDropY(0.0f) {}

    u32 mCurJump;
    bool mIsLongJumping;
    bool mIsHoverBurstValid;
    bool mIsPreservedHover;
    f32 mLastDropY;
    f32 mSlideSpeedMultiplier;
};

inline PlayerMovementData *getPlayerMovementData(TMario *player) {
    return reinterpret_cast<PlayerMovementData *>(
        Player::getRegisteredData(player, "_movement_data"));
}

inline PlayerMovementParams *getPlayerMovementParams(TMario *player) {
    return reinterpret_cast<PlayerMovementParams *>(
        Player::getRegisteredData(player, "_movement_params"));
}