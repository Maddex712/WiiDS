#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

s16 matan(f32 y, f32 x);
template <typename T> T MsClamp(T value, T min, T max);
f32 MsGetRotFromZaxisY(const TVec3f &);
bool MsIsInSight(const TVec3f &src, const TVec3f &other, f32 x, f32 y, f32 z);
void MsMtxSetXYZRPH(Mtx, f32 r, f32 p, f32 h);
void MsMtxSetRotX(Mtx, f32 r);
void MsMtxSetRotX(Mtx, f32 r);
void MsMtxSetRotY(Mtx, f32 r);
void MsMtxSetTRS(Mtx, f32 tx, f32 ty, f32 tz, f32 rx, f32 ry, f32 rz, f32 sx, f32 sy, f32 sz);
void MsMtxSetXYZRPH(Mtx, f32 r, f32 p, f32 h, s16, s16, s16);
void MsPerpendicFootToLineR(const TVec3f &, const TVec3f &, const TVec3f &);
f32 MsSqrtf(f32);
f32 MsCos(f32);
f32 MsSin(f32);
f32 MsVECMag2(Vec *);
f32 MsVECNormalize(Vec *src, Vec *out);
template <typename T> T MsWrap(T value, T min, T max);

void SMSCalcJumpVelocityXZ(const TVec3f &to, const TVec3f &from, f32 z, f32 y, TVec3f *out);