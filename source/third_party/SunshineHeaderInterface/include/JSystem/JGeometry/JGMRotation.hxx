#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/math.h>
#include <JSystem/JGeometry/JGMMatrix.hxx>
#include <JSystem/JGeometry/JGMQuat.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/equivtype.hxx>

namespace JGeometry {

    template <typename MatrixT = TMtx34f> struct TRotation3 {
        MatrixT mtx;

        TRotation3() = default;

        void getQuat(TQuat4f &dst) const;
        void identity33();
        void setEular(f32 x, f32 y, f32 z);
        void setLookDir(const TVec3f &forward, const TVec3f &up);
        void setQuat(const TQuat4f &src);
        void setRotate(const TVec3f &axis, f32 angle);
        void setSQ(const TVec3f &, const TQuat4f &);
    };

}  // namespace JGeometry

typedef JGeometry::TRotation3<> TRotation3f;