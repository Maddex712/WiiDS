#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/math.h>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/equivtype.hxx>

namespace JGeometry {

    template <typename T> struct TQuat4 {
        TVec3<T> v;
        T s;

        static TQuat4 identity() {
            return {
                {0, 0, 0},
                1
            };
        }

        static TQuat4 fromEuler(TVec3<T> angles) {
            f32 cr = cosf(angles.x * 0.5f);
            f32 sr = sinf(angles.x * 0.5f);
            f32 cp = cosf(angles.y * 0.5f);
            f32 sp = sinf(angles.y * 0.5f);
            f32 cy = cosf(angles.z * 0.5f);
            f32 sy = sinf(angles.z * 0.5f);

            TQuat4 q;
            q.s = cr * cp * cy + sr * sp * sy;
            q.v = {sr * cp * cy - cr * sp * sy,   // x
                   cr * sp * cy + sr * cp * sy,   // y
                   cr * cp * sy - sr * sp * cy};  // z

            return q;
        }

        void toEuler(TVec3<T> &dst) {}

        f32 norm() const { return sqrtf(s * s + v.dot()); }

        /* General operations */
        TQuat4 conjugate() const { return {v.negate(), s}; }
        TQuat4 inverse() const {
            const f32 aN = fabsf(norm());
            if (aN == 0.0f)
                return identity();

            return conjugate() * (1.0f / aN);
        }

        f32 angle(const TQuat4 &other) const {
            TQuat4 q12 = conjugate() * other;
            return 2.0f * atan2f(q12.norm(), q12.s);
        }

        TQuat4 lookRotation(const TVec3<T> &forward, const TVec3<T> &up) {
            TVec3<T> vF = forward;
            vF.normalize();

            TVec3<T> tvU = up;
            tvU.normalize();

            TVec3<T> vU;
            forward.cross(tvU, vU);

            TVec3<T> vR;
            vU.cross(vF, vR);

            const f32 m00 = vR.x;
            const f32 m01 = vU.x;
            const f32 m02 = vF.x;
            const f32 m10 = vR.y;
            const f32 m11 = vU.y;
            const f32 m12 = vF.y;
            const f32 m20 = vR.z;
            const f32 m21 = vU.z;
            const f32 m22 = vF.z;

            TQuat4 lookRot;
            lookRot.s       = sqrtf(1.0f + m00 + m11 + m22) * 0.5f;
            const f32 recip = 1.0f / (4.0f * lookRot.s);
            lookRot.v       = {(m21 - m12) * recip, (m02 - m20) * recip, (m10 - m01) * recip};

            return lookRot;
        }

        void normalize() {
            const f32 n = norm();
            if (n == 0)
                return;

            v *= 1.0f / n;
            s *= 1.0f / n;
        }

        void makeUnitNorm() {
            v.normalize();
            s = cosf(DEGREES_TO_RADIANS(s) * 0.5f);
            v = v * sinf(DEGREES_TO_RADIANS(s) * 0.5f);
        }

        void rotate(const TVec3<T> &axis, TVec3<T> &dst) const;
        void setRotate(const TVec3<T> &axis, const TVec3<T> &dst, f32 rotation);
        void slerp(const TQuat4 &target, f32 time);

        /* Operators */
        TQuat4 &operator=(const TQuat4 &other) {
            v = other.v;
            s = other.s;
            return *this;
        }

        TQuat4 &operator+=(const TQuat4 &other) {
            v += other.v;
            s += other.s;
            return *this;
        }

        TQuat4 &operator-=(const TQuat4 &other) {
            v -= other.v;
            s -= other.s;
            return *this;
        }

        TQuat4 &operator*=(const TQuat4 &other) {
            TVec3f cross;
            v.cross(other.v, cross);

            v = other.v * s + v * other.s + cross;
            s = s * other.s - v.dot(other.v);
            return *this;
        }

        TQuat4 &operator*=(const f32 scalar) {
            v *= scalar;
            s *= scalar;
            return *this;
        }

        TQuat4 &operator+(const TQuat4 &other) const { return {v + other.v, s + other.s}; }

        TQuat4 &operator-(const TQuat4 &other) const { return {v - other.v, s - other.s}; }

        TQuat4 &operator*(const TQuat4 &other) const {
            TVec3f cross;
            v.cross(other.v, cross);

            return {other.v * s + v * other.s + cross, s * other.s - v.dot(other.v)};
        }

        TQuat4 &operator*(const f32 scalar) const { return {v * scalar, s * scalar}; }
    };

}  // namespace JGeometry
typedef JGeometry::TQuat4<f32> TQuat4f;