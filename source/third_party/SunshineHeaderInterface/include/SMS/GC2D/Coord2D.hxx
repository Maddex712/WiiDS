#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

class TCoord2D {
public:
    void setValue(s32 speed, f32 startx, f32 starty, f32 endx, f32 endy);

    template <typename T> T roundf(f32) const;

    void update();

    f32 mStartX;
    f32 mStartY;
    f32 mEndX;
    f32 mEndY;
    f32 mUpdateSpeedX;
    f32 mUpdateSpeedY;
};