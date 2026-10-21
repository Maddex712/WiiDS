/* Port override of decomp/include/fake_tgmath.h: the float overloads it adds
 * (sqrt/fabs/floor) already exist in libstdc++'s <cmath>. */
#ifndef SMS_PORT_FAKE_TGMATH_H
#define SMS_PORT_FAKE_TGMATH_H
#include <dolphin/types.h>
#include <math.h>
#ifdef __cplusplus
#include <cmath>
using std::sqrt;
using std::fabs;
using std::floor;
#endif
#endif
