#pragma once
#include <cmath>

namespace port {
// One native camera turn per horizontal deflection. Sample once per camera frame; every
// camera and its sound code then sees the same request. Blocked scenes require a fresh nudge.
class CameraStick {
    int latched = 0;
    int request = 0;
public:
    void update(float x, bool enabled) {
        request = 0;
        if (!std::isfinite(x)) { latched = 0; return; }
        if (std::fabs(x) < .25f) { latched = 0; return; }
        const int direction = x < -.55f ? -1 : x > .55f ? 1 : 0;
        if (!enabled) { if (direction) latched = direction; return; }
        if (direction && direction != latched) { request = direction; latched = direction; }
    }
    bool left() const { return request < 0; }
    bool right() const { return request > 0; }
};
}
