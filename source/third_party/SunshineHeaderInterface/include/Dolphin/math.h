#ifndef MATH_H
#define MATH_H

#include <Dolphin/types.h>

#define M_PI               3.14159265358979323846
#define Max(a, b)          (((a) > (b)) ? (a) : (b))
#define Min(a, b)          (((a) < (b)) ? (a) : (b))
#define Clamp(a, min, max) (Max(Min(a, max), min))

#define DEGREES_TO_RADIANS(x) ((x) * (M_PI / 180.0f))
#define RADIANS_TO_DEGREES(x) ((x) * (180.0f / M_PI))

#define DBL_EPSILON 2.2204460492503131e-016
#define FLT_EPSILON 0x0.000002p0

#define NAN      0.0f / 0.0f
#define isnan(x) (x) != (x)

#ifdef __cplusplus
extern "C" {
#endif

f32 acosf(f32);
f64 atan(f64);
f32 atanf(f32);
f64 atan2(f64, f64);
f32 atan2f(f32, f32);
f64 cos(f64 rad);
f32 cosf(f32 rad);
f32 expf(f32 x);
f64 log(f64 x);
f64 fabs(f64 x);
f32 fabsf(f32 x);
f64 frexp(f64, int *);
f32 powf(f32 x, f32 power);
f64 sin(f64);
f32 sinf(f32 rad);
f32 tanf(f32 rad);
f64 sqrt(f64 x);
f32 sqrtf(f32 x);

#ifdef __cplusplus
}
#endif

#endif