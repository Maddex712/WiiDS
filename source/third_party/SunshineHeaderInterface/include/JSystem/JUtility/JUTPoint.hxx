#pragma once

#include <Dolphin/types.h>

class JUTPoint {
public:
    JUTPoint();
    inline JUTPoint(int x_, int y_) : x(x_), y(y_) {}

    int x;  // _0
    int y;  // _4
};