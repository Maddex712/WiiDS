#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/math.h>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/equivtype.hxx>

namespace JGeometry {

    template <typename VecT> struct TBox {
        VecT center, size;

        TBox() = default;
        TBox(const VecT &_center, const VecT &_size) : center(_center), size(_size) {}

        TBox &operator=(const TBox &other) {
            center = other.center;
            size   = other.size;
        }

        bool contains(VecT pos) const {
            const auto xHalf = size.x / 2;
            const auto yHalf = size.y / 2;
            const auto zHalf = size.z / 2;

            if (center.x - xHalf > pos.x || center.x + xHalf < pos.x)
                return false;

            if (center.y - yHalf > pos.y || center.y + yHalf < pos.y)
                return false;

            if (center.z - zHalf > pos.z || center.z + zHalf < pos.z)
                return false;

            return true;
        }
    };

}  // namespace JGeometry