#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>

struct J3DTransformInfo {
    f32 sx;
    f32 sy;
    f32 sz;
    s16 rx;
    s16 ry;
    s16 rz;
    f32 tx;
    f32 ty;
    f32 tz;
};

void J3DGetTranslateRotateMtx(s16 rx, s16 ry, s16 rz, f32 tx, f32 ty, f32 tz, Mtx out);
void J3DGetTranslateRotateMtx(const J3DTransformInfo &info, Mtx out);