#pragma once

#include <Dolphin/math.h>
#include <Dolphin/types.h>

namespace JGeometry {
    template <typename T> class TUtil {
    public:
        static T clamp(T x, T min, T max) {
            if (x < min)
                return min;
            if (x > max)
                return max;
            return x;
        }

        static f32 inv_sqrt(T x) {
            if (x == 0)
                return x;
            return 1.0f / sqrtf(static_cast<f32>(x));
        }

        static T mod(T x, T mod);
        static T one() { return 1; }
    };
}  // namespace JGeometry