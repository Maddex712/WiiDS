#pragma once

#include <Dolphin/types.h>
#include <SMS/Map/JointModel.hxx>
#include <SMS/Map/PollutionPos.hxx>

enum PollutionType {
    POLLUTION_SINK,
    POLLUTION_FIRE,
    POLLUTION_STOP,
    POLLUTION_SLIP,
    POLLUTION_ELEC,
    POLLUTION_SINK_DIE,
    POLLUTION_NULL1,
    POLLUTION_BURN,
    POLLUTION_NULL2,
    POLLUTION_NULL3,
    POLLUTION_NONE
};

class TPollutionLayer : public TJointModel {

public:
    u16 mPollutionEffect;  // 0x0030

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[2];
#else
    u8 _pc32_tail[2];
#endif
};