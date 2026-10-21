/* PC replacement for the body of dolphin/gx/GXVert.h.
 *
 * On the GameCube the vertex writers are inlines storing to the write-gather
 * pipe at 0xCC008000.  For TARGET_PC, GXVert.h should include this header
 * instead of defining them:
 *
 *     #ifdef TARGET_PC
 *     #include <sms_gx/gxvert_pc.h>
 *     #else
 *     ... original contents ...
 *     #endif
 *
 * The writers become calls into sms_gx, and in C++ `GXWGFifo.u8 = x` style
 * stores (MarioUtil/PacketUtil.cpp) go through GXPC_WGPipe.
 */
#ifndef _DOLPHIN_GX_GXVERT_H_
#define _DOLPHIN_GX_GXVERT_H_

#include <dolphin/types.h>
#include <sms_gx/gx_pc.h>

#ifdef __cplusplus
#define GXWGFifo GXPC_WGPipe
extern "C" {
#endif

#define SMS_GXV1(name, T) void name##1##T(T x);
#define SMS_GXV2(name, T) void name##2##T(T x, T y);
#define SMS_GXV3(name, T) void name##3##T(T x, T y, T z);
#define SMS_GXV4(name, T) void name##4##T(T x, T y, T z, T w);

SMS_GXV1(GXCmd, u8) SMS_GXV1(GXCmd, u16) SMS_GXV1(GXCmd, u32)
SMS_GXV1(GXParam, u8) SMS_GXV1(GXParam, u16) SMS_GXV1(GXParam, u32)
SMS_GXV1(GXParam, s8) SMS_GXV1(GXParam, s16) SMS_GXV1(GXParam, s32)
SMS_GXV1(GXParam, f32) SMS_GXV3(GXParam, f32) SMS_GXV4(GXParam, f32)
SMS_GXV3(GXPosition, f32) SMS_GXV3(GXPosition, u8) SMS_GXV3(GXPosition, s8)
SMS_GXV3(GXPosition, u16) SMS_GXV3(GXPosition, s16)
SMS_GXV2(GXPosition, f32) SMS_GXV2(GXPosition, u8) SMS_GXV2(GXPosition, s8)
SMS_GXV2(GXPosition, u16) SMS_GXV2(GXPosition, s16)
void GXPosition1x16(u16 x);
void GXPosition1x8(u8 x);
SMS_GXV3(GXNormal, f32) SMS_GXV3(GXNormal, s16) SMS_GXV3(GXNormal, s8)
void GXNormal1x16(u16 x);
void GXNormal1x8(u8 x);
SMS_GXV4(GXColor, u8) SMS_GXV1(GXColor, u32) SMS_GXV3(GXColor, u8) SMS_GXV1(GXColor, u16)
void GXColor1x16(u16 x);
void GXColor1x8(u8 x);
SMS_GXV2(GXTexCoord, f32) SMS_GXV2(GXTexCoord, s16) SMS_GXV2(GXTexCoord, u16)
SMS_GXV2(GXTexCoord, s8) SMS_GXV2(GXTexCoord, u8)
SMS_GXV1(GXTexCoord, f32) SMS_GXV1(GXTexCoord, s16) SMS_GXV1(GXTexCoord, u16)
SMS_GXV1(GXTexCoord, s8) SMS_GXV1(GXTexCoord, u8)
void GXTexCoord1x16(u16 x);
void GXTexCoord1x8(u8 x);
SMS_GXV1(GXMatrixIndex, u8)

#undef SMS_GXV1
#undef SMS_GXV2
#undef SMS_GXV3
#undef SMS_GXV4

#ifdef __cplusplus
}
#endif

#endif
