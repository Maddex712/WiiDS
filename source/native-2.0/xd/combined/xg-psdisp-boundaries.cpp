



#define HSD_FILE_NAME "psdisp.c"

#ifndef __MWERKS__
#include <game/psq.h>
#endif
#include <sysdolphin/baselib/psdisp.h>
#include "game/gc_fused.h"

#include <string.h>


#ifdef __MWERKS__
#define _MATH_H_
#ifdef __cplusplus
extern "C" f32 __float_nan[];
#else
extern f32 __float_nan[];
#endif
extern f32 __float_min[];
#define NAN (__float_nan[0])
double sin(double x);
double cos(double x);
double tan(double x);
double atan2(double y, double x);
double __fabs(double x);
#define fabs(x) __fabs(x)

#ifndef __MWERKS__
static
#endif
inline f32 sinf(f32 x)
{
    return (f32) sin(x);
}

#ifndef __MWERKS__
static
#endif
inline f32 cosf(f32 x)
{
    return (f32) cos(x);
}

#ifndef __MWERKS__
static
#endif
inline f32 tanf(f32 x)
{
    return (f32) tan(x);
}

inline int __fpclassifyf(float x)
{
    switch ((*(s32*) &x) & 0x7F800000) {
    case 0x7F800000:
        if ((*(s32*) &x) & 0x007FFFFF) {
            return 1;
        } else {
            return 2;
        }
    case 0:
        if ((*(s32*) &x) & 0x007FFFFF) {
            return 5;
        } else {
            return 3;
        }
    }
    return 4;
}

#ifndef __MWERKS__
static
#endif
inline float sqrtf(float x)
{
    if (x > 0.0f) {
        double guess = __frsqrte((double) x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        return (float) (x * guess);
    } else if (x < 0.0) {
        return NAN;
    } else if (__fpclassifyf(x) == 1) {
        return NAN;
    }
    return x;
}
#else
#include <math.h>
#include <float.h>
static const f32 psdisp_float_min[1] = { FLT_MIN };
#define __float_min psdisp_float_min
#endif

#ifdef __MWERKS__
#define PS_INLINE inline
#else

#define PS_INLINE static inline
#endif
PS_INLINE float atan2fInline(float y, float x)
{
    float r = (float) atan2(y, x);
    return r;
}

#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/forward.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/object.h>
#include <sysdolphin/baselib/particle.h>
#include <sysdolphin/baselib/psdisptev.h>
#include <sysdolphin/baselib/psstructs.h>
#ifndef __MWERKS__
#include <stddef.h>
static_assert(sizeof(HSD_Particle) == 0xB8, "pslist.cpp allocates 0xB8-byte particles on PC: keep both in step");

static_assert(offsetof(HSD_Generator, pos) == 0x28 && offsetof(HSD_Generator, camera) == 0x98 &&
                  offsetof(HSD_Generator, flags) == 0xA0 && offsetof(HSD_Generator, jobj) == 0xC0,
              "PSGenerator views (GSparticle.cpp, particle.cpp) must match HSD_Generator on PC");
#endif
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/util.h>
#include <dolphin/gx.h>

#pragma pool_strings off

#ifdef __MWERKS__

static inline void __OSf32tos16(register f32 arg, volatile register s16* out)
{
    asm { psq_st arg, 0(out), 1, 6 }
}

static inline s16 OSf32tos16(f32 in)
{
    s32 out;
    __OSf32tos16(in, (s16*) &out);
    return *(s16*) &out;
}
#else


static inline s16 psdisp_OSf32tos16(f32 in)
{
    s16 out;
    PSQ_STORE(&out, 0, in, 6);
    return out;
}
#define OSf32tos16 psdisp_OSf32tos16
#endif
#ifndef __MWERKS__

#undef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(num)
#endif

#define psPosition3(x, y, z)     GXPosition3s16(OSf32tos16(x), OSf32tos16(y), OSf32tos16(z))


#define psColorRGBA4(r, g, b, a)     GXColor1u16((((r) << 8) & 0xF000) | (((g) << 4) & 0xF00) | ((b) & 0xF0) | (((a) >> 4) & 0xF))
void HSD_MtxGetRotationMtx(Mtx m, Mtx out, char a, char b);
extern int __float_epsilon[];
#undef FLT_MIN
#define FLT_MIN (__float_min[0])
#undef FLT_EPSILON
#define FLT_EPSILON (*(f32*) __float_epsilon)

typedef struct { f32 x, y; } Vec2;



static u8 billboard_tex0_u8[32] ATTRIBUTE_ALIGN(32) ;
u8 psFrameNum = 123;
static Mtx vmtx;
static Mtx rvmtx;
static f32 prj[GX_PROJECTION_SZ];
static Mtx pvmtx;
static Mtx rvmtxRot;
static s32 prevPointSize;
static s32 prevLineWidth;
static f32 rx0;
static f32 rx1;
static f32 ry0;
static f32 ry1;
static f32 rz0;
static f32 rz1;
static s32 prevBlendMode;
static s32 prevChanCtrl;
static GXColor prevChanMat;
static GXColor prevChanAmb;
static GXColor prevColorPrim;
static GXColor prevColorEnv;
static GXColor prevColorMat;
static GXPosNrmMtx currentMtx;
static HSD_Fog* particleFog;

#ifdef MUST_MATCH
#pragma push
#pragma inline_depth(3)
#endif

#ifdef MUST_MATCH
#pragma pop
#endif

static inline void setVtxDesc(s32 fmt);
inline static void calcTornadoLastPos(HSD_Particle* pp, f32* x, f32* y, f32* z);
inline static void getColorPrimEnv(HSD_Particle* pp, GXColor* primCol, GXColor* envCol);
inline static void getColorMatAmb(HSD_Particle* pp, GXColor* matCol, GXColor* ambCol);
static inline void getClrTrail(HSD_Particle* pp, GXColor* color);
static inline void psSetColor(GXColor* color, u8 value);
static inline void psSetupVtxFormat(GXVtxFmt format, bool has_color,
                                    bool has_texture, GXCompType texture_type)
{
    GXSetVtxAttrFmt(format, GX_VA_POS, GX_POS_XYZ, GX_S16, 6U);
    if (has_color) {
        GXSetVtxAttrFmt(format, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA4, 0U);
    }
    if (has_texture) {
        GXSetVtxAttrFmt(format, GX_VA_TEX0, GX_TEX_ST, texture_type, 0U);
    }
}

static inline void psSetupVtxFormat(GXVtxFmt format, bool has_color, bool has_texture, GXCompType texture_type);
static inline void setupChanCtrl(HSD_Particle* pp);
static inline void setBlendMode(int blend_mode);
static inline void psSetCurrentMtx(GXPosNrmMtx idx);
static inline void psMaskAbsF32(f32* value);
static inline bool psMaskAbsLtF32(f32 value, f32 limit);
static inline bool psMaskAbsGtF32(f32 value, f64 limit);
static inline bool psAbsLtF32(f32 value, f32 limit);
static inline bool psAbsGtF32(f32 value, f64 limit);
static inline bool psDispSubAbsLtF32(f32 value, f32 limit);
static inline bool psDispSubAbsGtF32(f32 value, f64 limit);
static inline void psScaleAppSRTAxes(HSD_Particle* pp, Mtx mtx);
static inline void psUpdateBillboardAxes(const Mtx inv_view);
PS_INLINE void setupChanReg(HSD_Particle* pp);
PS_INLINE void setupTevReg(HSD_Particle* pp);
PS_INLINE HSD_Particle* psDispSubPointTrail(HSD_Particle* pp);
PS_INLINE void psDispSubMakePolygon(HSD_Particle* pp, u8* texform, f32 x, f32 y, f32 z, f32 vx, f32 vy, f32 vz, f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1);
PS_INLINE void psDispSub(HSD_Particle* pp, u8* texform);
PS_INLINE void psDispSubAPPSRTPoint(HSD_Particle* pp);
PS_INLINE void psDispSubAppSRT(HSD_Particle* pp, u8* texform);

void psDispStrippedC(void* o)
{
    ref_INC(o);
}



void psDispStrippedA(Mtx out)
{
    Vec3 scale = { 1.0F, 1.0F, 1.0F };
    Vec3 translate = { 0.0F, 0.0F, 0.0F };
    Quaternion rot = { 0.0F, 0.0F, 0.0F, 0.0F };

    HSD_MtxSRT(out, &scale, (Vec3*) &rot, &translate, NULL);
}

void psDispStrippedB(Mtx out)
{
    Vec3 scale = { 1.0F, 1.0F, 1.0F };
    Vec3 translate = { 0.0F, 0.0F, 0.0F };
    Quaternion rot = { 0.0F, 0.0F, 0.0F, 0.0F };

    HSD_MtxSRT(out, &scale, (Vec3*) &rot, &translate, NULL);
}

void psDispBeginFrame(void)
{
    if (psFrameNum >= 0xFF) {
        psFrameNum = 1;
    } else {
        psFrameNum += 1;
    }
}

int psDispHasRenderData(u32 target_link)
{
    s32 i;
    HSD_Particle* texedge;
    HSD_Particle* xlu;

    for (i = 0; i < PS_NUM_LINK; i++) {
        if (target_link & (1 << i)) {
            particleSort(i, &texedge, &xlu);
            if (texedge != NULL || xlu != NULL) {
                return 1;
            }
        }
    }
    return 0;
}

static inline void setVtxDesc(s32 fmt)
{
    GXClearVtxDesc();
    GXInvalidateVtxCache();
    switch (fmt) {
    case 0:
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);
        return;
    case 1:
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        return;
    case 2:
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);
        return;
    case 3:
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        return;
    case 4:
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        return;
    case 5:
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        return;
    }
}

inline static void calcTornadoLastPos(HSD_Particle* pp, f32* x, f32* y, f32* z)
{
    f32 radius;
    f32 px, py, pz;
    f32 sina, sinb, cosa, cosb;
    HSD_Generator* gp;
    f32 vx0;
    f32 vz0;

    gp = pp->gen;
    if (gp == NULL) {
        *x = pp->pos.x;
        *y = pp->pos.y;
        *z = pp->pos.z;
        return;
    }

    sina = sinf(pp->grav);
    sinb = sinf(pp->fric);
    cosa = cosf(pp->grav);
    cosb = cosf(pp->fric);

    vz0 = pp->vel.z - gp->aux.tornado.vel;
    vx0 = pp->vel.x - gp->grav;

    radius = gp->radius < 0.0f ? -gp->radius : gp->radius;
    radius += vz0 * tanf(gp->angle < 0.0f ? -gp->angle : gp->angle);
    radius *= pp->vel.y;
    px = radius * cosf(vx0);
    py = radius * sinf(vx0);
    pz = vz0;

    *x = px * cosb + pz * sinb + gp->pos.x;
    *y = -px * sina * sinb + py * cosa + pz * sina * cosb + gp->pos.y;
    *z = -px * cosa * sinb - py * sina + pz * cosa * cosb + gp->pos.z;
}



inline static void calcTornadoLastPosAPPSRT(HSD_Particle* pp, f32* x, f32* y, f32* z)
{
    Mtx mtx;
    Vec3 v;

    calcTornadoLastPos(pp, x, y, z);
    HSD_MtxSRT(mtx, &pp->appsrt->scale, (Vec3*) &pp->appsrt->rot, &pp->appsrt->translate, NULL);
    v.x = *x;
    v.y = *y;
    v.z = *z;
    PSMTXMultVec(mtx, (Vec*) &v, (Vec*) &v);
    *x = v.x;
    *y = v.y;
    *z = v.z;
}

inline static void getColorPrimEnv(HSD_Particle* pp, GXColor* primCol,
                            GXColor* envCol)
{
    if (pp->primColCount) {
        int scale = 65536 * pp->primColRemain / pp->primColCount;
        primCol->r = ((pp->primColTarget.r << 16) +
                      (pp->primCol.r - pp->primColTarget.r) * scale) >>
                     16;
        primCol->g = ((pp->primColTarget.g << 16) +
                      (pp->primCol.g - pp->primColTarget.g) * scale) >>
                     16;
        primCol->b = ((pp->primColTarget.b << 16) +
                      (pp->primCol.b - pp->primColTarget.b) * scale) >>
                     16;
        primCol->a = ((pp->primColTarget.a << 16) +
                      (pp->primCol.a - pp->primColTarget.a) * scale) >>
                     16;
    } else {
        *primCol = pp->primCol;
    }
    if (pp->envColCount) {
        int scale = 65536 * pp->envColRemain / pp->envColCount;
        envCol->r = ((pp->envColTarget.r << 16) +
                     (pp->envCol.r - pp->envColTarget.r) * scale) >>
                    16;
        envCol->g = ((pp->envColTarget.g << 16) +
                     (pp->envCol.g - pp->envColTarget.g) * scale) >>
                    16;
        envCol->b = ((pp->envColTarget.b << 16) +
                     (pp->envCol.b - pp->envColTarget.b) * scale) >>
                    16;
        envCol->a = ((pp->envColTarget.a << 16) +
                     (pp->envCol.a - pp->envColTarget.a) * scale) >>
                    16;
    } else {
        *envCol = pp->envCol;
    }
}

inline static void getColorMatAmb(HSD_Particle* pp, GXColor* matCol, GXColor* ambCol)
{
    if (pp->matColCount) {
        int scale = 65536 * pp->matColRemain / pp->matColCount;
        u8 rgb = ((pp->matRGBTarget << 16) +
                  (pp->matRGB - pp->matRGBTarget) * scale) >>
                 16;
        matCol->r = matCol->g = matCol->b = rgb;
        matCol->a =
            ((pp->matATarget << 16) + (pp->matA - pp->matATarget) * scale) >>
            16;
    } else {
        matCol->r = matCol->g = matCol->b = pp->matRGB;
        matCol->a = pp->matA;
    }
    if (pp->ambColCount) {
        int scale = 65536 * pp->ambColRemain / pp->ambColCount;
        u8 rgb = ((pp->ambRGBTarget << 16) +
                  (pp->ambRGB - pp->ambRGBTarget) * scale) >>
                 16;
        ambCol->r = ambCol->g = ambCol->b = rgb;
        ambCol->a =
            ((pp->ambATarget << 16) + (pp->ambA - pp->ambATarget) * scale) >>
            16;
    } else {
        ambCol->r = ambCol->g = ambCol->b = pp->ambRGB;
        ambCol->a = pp->ambA;
    }
}

static inline void getClrTrail(HSD_Particle* pp, GXColor* color)
{
    GXColor env_color;

    switch (pp->kind & (DispLighting | PrimEnv)) {
    case 0:
    case DispLighting:
        getColorPrimEnv(pp, color, &env_color);
        break;
    case PrimEnv:
    case DispLighting | PrimEnv:
        color->r = color->g = color->b = color->a = 0xFF;
        break;
    }
}

static inline void psSetColor(GXColor* color, u8 value)
{
    color->r = value;
    color->g = value;
    color->b = value;
    color->a = value;
}

static inline void setupChanCtrl(HSD_Particle* pp)
{
    u32 chan_state = pp->kind & (DispLighting | Trail);

    if (chan_state != (u32) prevChanCtrl) {
        prevChanCtrl = chan_state;
        GXSetNumChans(1);
        switch (prevChanCtrl) {
        case Trail:
            GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, 0,
                          GX_DF_NONE, GX_AF_NONE);
            break;
        case DispLighting:
            GXSetChanCtrl(GX_COLOR0, GX_TRUE, GX_SRC_REG, GX_SRC_REG,
                          HSD_LObjGetLightMaskDiffuse(), GX_DF_NONE,
                          HSD_LObjGetLightMaskAttnFunc() ? GX_AF_SPOT
                                                         : GX_AF_NONE);
            GXSetChanCtrl(GX_ALPHA0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0,
                          GX_DF_NONE, GX_AF_NONE);
            break;
        case DispLighting | Trail:
            GXSetChanCtrl(GX_COLOR0, GX_TRUE, GX_SRC_REG, GX_SRC_REG,
                          HSD_LObjGetLightMaskDiffuse(), GX_DF_NONE,
                          HSD_LObjGetLightMaskAttnFunc() ? GX_AF_SPOT
                                                         : GX_AF_NONE);
            GXSetChanCtrl(GX_ALPHA0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, 0,
                          GX_DF_NONE, GX_AF_NONE);
            break;
        default:
            GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0,
                          GX_DF_NONE, GX_AF_NONE);
            break;
        }
    }
}

static inline void setBlendMode(int blend_mode)
{
    if (prevBlendMode != blend_mode) {
        prevBlendMode = blend_mode;
        switch (blend_mode) {
        case 0:
            GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
                           GX_LO_CLEAR);
            break;
        case 1:
            GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE,
                           GX_LO_CLEAR);
            break;
        case 2:
            GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
                           GX_LO_CLEAR);
            break;
        case 3:
            GXSetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_ZERO,
                           GX_LO_CLEAR);
            break;
        default:
            OSReport("Particle:setBlendMode:Unknown mode\n");
            break;
        }
    }
}

static inline void psSetCurrentMtx(GXPosNrmMtx idx)
{
    if (currentMtx != idx) {
        currentMtx = idx;
        GXSetCurrentMtx(idx);
    }
}

static inline void psMaskAbsF32(f32* value)
{
    *(s32*) value &= 0x7FFFFFFF;
}




static inline bool psMaskAbsLtF32(f32 value, f32 limit)
{
    psMaskAbsF32(&value);
    return value < limit;
}

static inline bool psMaskAbsGtF32(f32 value, f64 limit)
{
    psMaskAbsF32(&value);
    return value > limit;
}




static inline bool psAbsLtF32(f32 value, f32 limit)
{
    return fabs(value) < limit;
}

static inline bool psAbsGtF32(f32 value, f64 limit)
{
    return fabs(value) > limit;
}

static inline bool psDispSubAbsLtF32(f32 value, f32 limit)
{
    return psAbsLtF32(value, limit);
}

static inline bool psDispSubAbsGtF32(f32 value, f64 limit)
{
    return psAbsGtF32(value, limit);
}

static inline void psScaleAppSRTAxes(HSD_Particle* pp, Mtx mtx)
{
    mtx[0][0] *= pp->size;
    mtx[1][0] *= pp->size;
    mtx[2][0] *= pp->size;
    mtx[0][1] *= pp->size;
    mtx[1][1] *= pp->size;
    mtx[2][1] *= pp->size;
    mtx[0][2] *= pp->size;
    mtx[1][2] *= pp->size;
    mtx[2][2] *= pp->size;
}

static inline void psUpdateBillboardAxes(const Mtx inv_view)
{
    f32 right_x;
    f32 up_x;
    f32 right_y;
    f32 up_y;
    f32 right_z;
    f32 up_z;

    right_x = inv_view[0][0];
    up_x = inv_view[0][1];
    rx0 = right_x + up_x;
    rx1 = right_x - up_x;
    right_y = inv_view[1][0];
    up_y = inv_view[1][1];
    ry0 = right_y + up_y;
    ry1 = right_y - up_y;
    right_z = inv_view[2][0];
    up_z = inv_view[2][1];
    rz0 = right_z + up_z;
    rz1 = right_z - up_z;
}

PS_INLINE void setupChanReg(HSD_Particle* pp)
{
    GXColor prim_color;
    GXColor amb_color;
    GXColor mat_color;
    HSD_LObj* lobj;

    if (pp->kind & DispLighting) {
        getColorMatAmb(pp, &mat_color, &amb_color);
        if (pp->kind & PrimEnv) {
            prim_color.r = prim_color.g = prim_color.b = 0xFF;
        } else {
            getColorPrimEnv(pp, &prim_color, &mat_color);
            amb_color.r = (u8) ((amb_color.r * prim_color.r) >> 8);
            amb_color.g = (u8) ((amb_color.g * prim_color.g) >> 8);
            amb_color.b = (u8) ((amb_color.b * prim_color.b) >> 8);
        }
        if (prim_color.r != prevChanMat.r || prim_color.g != prevChanMat.g ||
            prim_color.b != prevChanMat.b)
        {
            prevChanMat = prim_color;
            GXSetChanMatColor(GX_COLOR0, prevChanMat);
        }
        lobj = HSD_LObjGetActiveByID(GX_MAX_LIGHT);
        if (lobj != NULL) {
            HSD_MulColor(&amb_color, &lobj->color, &amb_color);
        } else {
            amb_color.r = amb_color.g = amb_color.b = 0;
        }
        if (amb_color.r != prevChanAmb.r || amb_color.g != prevChanAmb.g ||
            amb_color.b != prevChanAmb.b)
        {
            prevChanAmb = amb_color;
            GXSetChanAmbColor(GX_COLOR0, prevChanAmb);
        }
    }
}

PS_INLINE void setupTevReg(HSD_Particle* pp)
{
    GXColor prim_color;
    GXColor env_color;
    GXColor mat_color;
    GXColor amb_color;

    getColorPrimEnv(pp, &prim_color, &env_color);
    if ((pp->kind & PrimEnv) ||
        (!(pp->kind & DispLighting) && !(pp->kind & Trail)))
    {
        if (prevColorPrim.r != prim_color.r ||
            prevColorPrim.g != prim_color.g ||
            prevColorPrim.b != prim_color.b || prevColorPrim.a != prim_color.a)
        {
            prevColorPrim = prim_color;
            GXSetTevColor(GX_TEVREG0, prevColorPrim);
        }
        if (pp->kind & PrimEnv) {
            if (prevColorEnv.r != env_color.r ||
                prevColorEnv.g != env_color.g ||
                prevColorEnv.b != env_color.b || prevColorEnv.a != env_color.a)
            {
                prevColorEnv = env_color;
                GXSetTevColor(GX_TEVREG1, prevColorEnv);
            }
        } else if (prevColorEnv.r != 0 || prevColorEnv.g != 0 ||
                   prevColorEnv.b != 0 || prevColorEnv.a != 0)
        {
            prevColorEnv.r = prevColorEnv.g = prevColorEnv.b = prevColorEnv.a =
                0;
            GXSetTevColor(GX_TEVREG1, prevColorEnv);
        }
    }
    if (pp->kind & DispLighting) {
        getColorMatAmb(pp, &mat_color, &amb_color);
        if (pp->kind & PrimEnv) {
            if (prevColorMat.r != mat_color.r ||
                prevColorMat.g != mat_color.g ||
                prevColorMat.b != mat_color.b || prevColorMat.a != mat_color.a)
            {
                prevColorMat = mat_color;
                GXSetTevColor(GX_TEVREG2, prevColorMat);
            }
        } else {
            mat_color.a = (u8) ((mat_color.a * prim_color.a) >> 8);
            if (prevColorMat.r != mat_color.r ||
                prevColorMat.g != mat_color.g ||
                prevColorMat.b != mat_color.b || prevColorMat.a != mat_color.a)
            {
                prevColorMat = mat_color;
                GXSetTevColor(GX_TEVREG2, prevColorMat);
            }
        }
    }
}

PS_INLINE void psDispSubAPPSRTPoint(HSD_Particle* pp)
{
    Mtx mtx;
    Mtx srt;
    Vec3 v;
    Vec3 tv;
    f32 lx, ly, lz;
    f32 px, py, pz;
    f32 vx, vy, vz;
    u8 w;

    psSetCurrentMtx(GX_PNMTX0);
    if (pp->appsrt->frameNum != psFrameNum) {
        if (pp->appsrt->status != PS_APPSTATUS_STILL) {
            HSD_MtxSRT(pp->appsrt->mmtx, &pp->appsrt->scale,
                       (Vec3*) &pp->appsrt->rot, &pp->appsrt->translate,
                       NULL);
        }
        if (pp->appsrt->status == PS_APPSTATUS_ONCE) {
            pp->appsrt->status = PS_APPSTATUS_STILL;
        }
    }
    pp->appsrt->frameNum = psFrameNum;
    PSMTXCopy(pp->appsrt->mmtx, mtx);
    mtx[0][3] -= pp->appsrt->translate.x;
    mtx[1][3] -= pp->appsrt->translate.y;
    mtx[2][3] -= pp->appsrt->translate.z;
    v.x = pp->vel.x;
    v.y = pp->vel.y;
    v.z = pp->vel.z;
    PSMTXMultVec(mtx, &v, &v);
    if (pp->appsrt->xA2 != 0) {
        PSMTXMultVec(rvmtxRot, &v, &v);
    }
    vx = v.x;
    vy = v.y;
    vz = v.z;
    v.x = pp->pos.x;
    v.y = pp->pos.y;
    v.z = pp->pos.z;
    if (pp->appsrt->xA2 != 0) {
        PSMTXMultVec(mtx, &v, &v);
        PSMTXMultVec(rvmtxRot, &v, &v);
        v.x += pp->appsrt->translate.x;
        v.y += pp->appsrt->translate.y;
        v.z += pp->appsrt->translate.z;
    } else {
        PSMTXMultVec(pp->appsrt->mmtx, &v, &v);
    }
    px = v.x;
    py = v.y;
    pz = v.z;
    if (pp->kind & Tornado) {
        calcTornadoLastPos(pp, &lx, &ly, &lz);
        HSD_MtxSRT(srt, &pp->appsrt->scale, (Vec3*) &pp->appsrt->rot,
                   &pp->appsrt->translate, NULL);
        tv.x = lx;
        tv.y = ly;
        tv.z = lz;
        PSMTXMultVec(srt, &tv, &tv);
        lx = tv.x;
        ly = tv.y;
        lz = tv.z;
    } else {
        lx = px - vx;
        ly = py - vy;
        lz = pz - vz;
    }
    w = (pp->size > 42.5) ? 255.0f : 6.0f * pp->size;
    if (pp->kind & Trail) {
        GXColor color;

        if (prevLineWidth != (s32) w) {
            prevLineWidth = w;
            GXSetLineWidth(w, GX_TO_ONE);
        }
        getClrTrail(pp, &color);
        if (pp->kind & DispTexture) {
            setVtxDesc(2);
            GXBegin(GX_LINES, GX_VTXFMT2, 2);
        } else {
            setVtxDesc(3);
            GXBegin(GX_LINES, GX_VTXFMT3, 2);
        }
        psPosition3(lx, ly, lz);
        psColorRGBA4(color.r, color.g, color.b,
                     (u8) ((f32) color.a * pp->trail));
        if (pp->kind & DispTexture) {
            GXTexCoord1x8(0);
        }
        psPosition3(px, py, pz);
        psColorRGBA4(color.r, color.g, color.b, color.a);
        if (pp->kind & DispTexture) {
            GXTexCoord1x8(1);
        }
    GXEnd(); // Aurora requires an explicit primitive boundary.
    } else {
        if (prevPointSize != (s32) w) {
            prevPointSize = w;
            GXSetPointSize(w, GX_TO_ONE);
        }
        if (pp->kind & DispTexture) {
            setVtxDesc(0);
            GXBegin(GX_POINTS, GX_VTXFMT0, 1);
        } else {
            setVtxDesc(1);
            GXBegin(GX_POINTS, GX_VTXFMT1, 1);
        }
        psPosition3(px, py, pz);
        if (pp->kind & DispTexture) {
            GXTexCoord1x8(1);
        }
    GXEnd(); // Aurora requires an explicit primitive boundary.
    }
}

PS_INLINE HSD_Particle* psDispSubPointTrail(HSD_Particle* pp)
{
    u8 w;
    GXColor color;
    f32 x, y, z;

    psSetCurrentMtx(GX_PNMTX0);
    w = (pp->size > 42.5) ? 255.0f : 6.0f * pp->size;
    if (prevLineWidth != (s32) w) {
        prevLineWidth = w;
        GXSetLineWidth(w, GX_TO_ONE);
    }
    if (pp->kind & Tornado) {
        calcTornadoLastPos(pp, &x, &y, &z);
    } else {
        x = pp->pos.x - pp->vel.x;
        y = pp->pos.y - pp->vel.y;
        z = pp->pos.z - pp->vel.z;
    }
    getClrTrail(pp, &color);
    if (pp->kind & DispTexture) {
        setVtxDesc(2);
        GXBegin(GX_LINES, GX_VTXFMT2, 2);
    } else {
        setVtxDesc(3);
        GXBegin(GX_LINES, GX_VTXFMT3, 2);
    }
    psPosition3(x, y, z);
    psColorRGBA4(color.r, color.g, color.b, (u8) ((f32) color.a * pp->trail));
    if (pp->kind & DispTexture) {
        GXTexCoord1x8(0);
    }
    psPosition3(pp->pos.x, pp->pos.y, pp->pos.z);
    psColorRGBA4(color.r, color.g, color.b, color.a);
    if (pp->kind & DispTexture) {
        GXTexCoord1x8(1);
    }
    GXEnd(); // Aurora requires an explicit primitive boundary.
    return pp;
}

PS_INLINE void psDispSubAppSRT(HSD_Particle* pp, u8* texform)
{
    Mtx mtx;
    Vec3 v;
    f32 right_x;
    f32 up_x;
    f32 right_y;
    f32 up_y;
    f32 right_z;
    f32 up_z;
    f32 x;
    f32 y;
    f32 z;
    f32 vx;
    f32 vy;
    f32 vz;
    f32 angle;
    f32 dir_x;
    f32 dir_y;
    HSD_Generator* gp;

    if (pp->appsrt->frameNum != psFrameNum) {
        if (pp->appsrt->status != PS_APPSTATUS_STILL) {
            HSD_MtxSRT(pp->appsrt->mmtx, &pp->appsrt->scale, (Vec3*) &pp->appsrt->rot,
                       &pp->appsrt->translate, NULL);
        }
        if (pp->appsrt->status == PS_APPSTATUS_ONCE) {
            pp->appsrt->status = PS_APPSTATUS_STILL;
        }
    }
    pp->appsrt->frameNum = psFrameNum;
    PSMTXCopy(pp->appsrt->mmtx, mtx);
    mtx[0][3] -= pp->appsrt->translate.x;
    mtx[1][3] -= pp->appsrt->translate.y;
    mtx[2][3] -= pp->appsrt->translate.z;
    v.x = pp->vel.x;
    v.y = pp->vel.y;
    v.z = pp->vel.z;
    PSMTXMultVec(mtx, &v, &v);
    if (pp->appsrt->xA2 != 0 && !(pp->kind & Tornado)) {
        PSMTXMultVec(rvmtxRot, &v, &v);
    }
    vx = v.x;
    vy = v.y;
    vz = v.z;
    v.x = pp->pos.x;
    v.y = pp->pos.y;
    v.z = pp->pos.z;
    if (pp->appsrt->xA2 != 0) {
        PSMTXMultVec(mtx, &v, &v);
        PSMTXMultVec(rvmtxRot, &v, &v);
        v.x += pp->appsrt->translate.x;
        v.y += pp->appsrt->translate.y;
        v.z += pp->appsrt->translate.z;
    } else {
        PSMTXMultVec(pp->appsrt->mmtx, &v, &v);
    }
    x = v.x;
    y = v.y;
    z = v.z;
    if (texform != NULL) {
        Mtx smtx;

        PSMTXScale(smtx, pp->appsrt->scale.x, pp->appsrt->scale.y, 1.0f);
        PSMTXConcat(rvmtx, smtx, smtx);
        right_x = smtx[0][0] * pp->size;
        up_x = -smtx[0][1] * pp->size;
        right_y = smtx[1][0] * pp->size;
        up_y = -smtx[1][1] * pp->size;
        right_z = smtx[2][0] * pp->size;
        up_z = -smtx[2][1] * pp->size;
    } else {
        Vec3 r;
        Vec3 u;

        r.x = pp->appsrt->scale.x + vmtx[0][3];
        r.y = pp->appsrt->scale.y + vmtx[1][3];
        r.z = vmtx[2][3];
        u.x = pp->appsrt->scale.x + vmtx[0][3];
        u.y = -pp->appsrt->scale.y + vmtx[1][3];
        u.z = vmtx[2][3];
        PSMTXMultVec(rvmtx, &r, &r);
        right_x = r.x * pp->size;
        right_y = r.y * pp->size;
        right_z = r.z * pp->size;
        PSMTXMultVec(rvmtx, &u, &u);
        up_x = u.x * pp->size;
        up_y = u.y * pp->size;
        up_z = u.z * pp->size;
    }
    gp = pp->gen;
    if (gp != NULL && (gp->flags & 0x20)) {
        right_x *= gp->scale.x;
        up_x *= gp->scale.x;
        right_y *= gp->scale.y;
        up_y *= gp->scale.y;
        right_z *= gp->scale.z;
        up_z *= gp->scale.z;
    }
    if ((pp->kind & Trail) || (pp->kind & DirVec)) {
        if (0.0f == prj[0]) {
            f32 prev_x;
            f32 prev_y;
            f32 prev_z;
            f32 w1inv;
            f32 w1;
            f32 w0inv;
            f32 w0;

            if (pp->kind & Tornado) {
                calcTornadoLastPos(pp, &prev_x, &prev_y, &prev_z);
                v.x = prev_x;
                v.y = prev_y;
                v.z = prev_z;
                if (pp->appsrt->xA2 != 0) {
                    PSMTXMultVec(mtx, &v, &v);
                    PSMTXMultVec(rvmtxRot, &v, &v);
                    v.x += pp->appsrt->translate.x;
                    v.y += pp->appsrt->translate.y;
                    v.z += pp->appsrt->translate.z;
                } else {
                    PSMTXMultVec(pp->appsrt->mmtx, &v, &v);
                }
                prev_x = v.x;
                prev_y = v.y;
                prev_z = v.z;
            } else {
                prev_x = x - vx;
                prev_y = y - vy;
                prev_z = z - vz;
            }
            w0 = vmtx[2][3] + (GC_MADD(vmtx[2][2], z, (GC_MADD(vmtx[2][0], x, vmtx[2][1] * y))));
            if (0.0f == w0) {
                goto no_direction;
            }
            w0inv = -1.0f / w0;
            w1 = vmtx[2][3] + (GC_MADD(vmtx[2][2], prev_z, (GC_MADD(vmtx[2][0], prev_x, vmtx[2][1] * prev_y))));
            if (0.0f == w1) {
                goto no_direction;
            }
            w1inv = -1.0f / w1;
            dir_x = GC_MSUB(w0inv, (pvmtx[0][3] + (GC_MADD(pvmtx[0][2], z, (GC_MADD(pvmtx[0][0], x, pvmtx[0][1] * y))))), w1inv * (pvmtx[0][3] + (GC_MADD(pvmtx[0][2], prev_z, (GC_MADD(pvmtx[0][0], prev_x, pvmtx[0][1] * prev_y))))));
            dir_y = GC_MSUB(w0inv, (pvmtx[1][3] + (GC_MADD(pvmtx[1][2], z, (GC_MADD(pvmtx[1][0], x, pvmtx[1][1] * y))))), w1inv * (pvmtx[1][3] + (GC_MADD(pvmtx[1][2], prev_z, (GC_MADD(pvmtx[1][0], prev_x, pvmtx[1][1] * prev_y))))));
        } else if (pp->kind & Tornado) {
            f32 prev_x;
            f32 prev_y;
            f32 prev_z;
            f32 dx;
            f32 dy;
            f32 dz;

            calcTornadoLastPos(pp, &prev_x, &prev_y, &prev_z);
            v.x = prev_x;
            v.y = prev_y;
            v.z = prev_z;
            if (pp->appsrt->xA2 != 0) {
                PSMTXMultVec(mtx, &v, &v);
                PSMTXMultVec(rvmtxRot, &v, &v);
                v.x += pp->appsrt->translate.x;
                v.y += pp->appsrt->translate.y;
                v.z += pp->appsrt->translate.z;
            } else {
                PSMTXMultVec(pp->appsrt->mmtx, &v, &v);
            }
            dx = x - v.x;
            dy = y - v.y;
            dz = z - v.z;
            dir_x = GC_MADD(pvmtx[0][2], dz, (GC_MADD(pvmtx[0][0], dx, pvmtx[0][1] * dy)));
            dir_y = GC_MADD(pvmtx[1][2], dz, (GC_MADD(pvmtx[1][0], dx, pvmtx[1][1] * dy)));
        } else {
            dir_x = GC_MADD(pvmtx[0][2], vz, (GC_MADD(pvmtx[0][0], vx, pvmtx[0][1] * vy)));
            dir_y = GC_MADD(pvmtx[1][2], vz, (GC_MADD(pvmtx[1][0], vx, pvmtx[1][1] * vy)));
        }
        if (fabs(dir_y) < FLT_MIN) {
            angle = (dir_x >= 0.0f) ? 1.5707964f : -1.5707964f;
        } else {
            angle = atan2fInline(dir_x, dir_y);
        }
        if (pp->kind & DirVec) {
            angle += pp->rotate;
        }
    } else {
    no_direction:
        angle = pp->rotate;
    }
    if (fabs(angle) > 0.01) {
        Mtx rmtx;
        Vec3 axis;
        f32 rx = right_x;
        f32 ry = right_y;
        f32 rz = right_z;
        f32 ux = up_x;
        f32 uy = up_y;
        f32 uz = up_z;

        axis.x = GC_MSUB(ry, uz, rz * uy);
        axis.y = GC_MSUB(rz, ux, rx * uz);
        axis.z = GC_MSUB(rx, uy, ry * ux);
        PSMTXRotAxisRad(rmtx, &axis, angle);
        right_x = GC_MADD(rmtx[0][2], rz, (GC_MADD(rmtx[0][0], rx, rmtx[0][1] * ry)));
        right_y = GC_MADD(rmtx[1][2], rz, (GC_MADD(rmtx[1][0], rx, rmtx[1][1] * ry)));
        right_z = GC_MADD(rmtx[2][2], rz, (GC_MADD(rmtx[2][0], rx, rmtx[2][1] * ry)));
        up_x = GC_MADD(rmtx[0][2], uz, (GC_MADD(rmtx[0][0], ux, rmtx[0][1] * uy)));
        up_y = GC_MADD(rmtx[1][2], uz, (GC_MADD(rmtx[1][0], ux, rmtx[1][1] * uy)));
        up_z = GC_MADD(rmtx[2][2], uz, (GC_MADD(rmtx[2][0], ux, rmtx[2][1] * uy)));
    }
    psDispSubMakePolygon(pp, texform, x, y, z, vx, vy, vz, right_x, right_y,
                         right_z, up_x, up_y, up_z);
}

PS_INLINE void psDispSubMakePolygon(HSD_Particle* pp, u8* texform, f32 x, f32 y, f32 z,
                          f32 vx, f32 vy, f32 vz, f32 rx, f32 ry, f32 rz,
                          f32 ux, f32 uy, f32 uz)
{
    GXColor color;

    psSetCurrentMtx(GX_PNMTX0);
    if (pp->kind & Trail) {
        f32 prev_x;
        f32 prev_y;
        f32 prev_z;

        if (pp->kind & Tornado) {
            if (pp->appsrt != NULL) {
                calcTornadoLastPosAPPSRT(pp, &prev_x, &prev_y, &prev_z);
            } else {
                calcTornadoLastPos(pp, &prev_x, &prev_y, &prev_z);
            }
        } else {
            prev_x = x - vx;
            prev_y = y - vy;
            prev_z = z - vz;
        }
        getClrTrail(pp, &color);
        if (texform == NULL) {
            if (pp->kind & DispTexture) {
                setVtxDesc(2);
                GXBegin(GX_QUADS, GX_VTXFMT2, 4);
            } else {
                setVtxDesc(3);
                GXBegin(GX_QUADS, GX_VTXFMT3, 4);
            }
            psPosition3(prev_x - rx, prev_y - ry, prev_z - rz);
            psColorRGBA4(color.r, color.g, color.b,
                       (u8) ((f32) color.a * pp->trail));
            if (pp->kind & DispTexture) {
                GXTexCoord1x8((pp->kind >> 16) & 0xC);
            }
            psPosition3(x - ux, y - uy, z - uz);
            psColorRGBA4(color.r, color.g, color.b, color.a);
            if (pp->kind & DispTexture) {
                GXTexCoord1x8(((pp->kind >> 16) & 0xC) + 1);
            }
            psPosition3(x + rx, y + ry, z + rz);
            psColorRGBA4(color.r, color.g, color.b, color.a);
            if (pp->kind & DispTexture) {
                GXTexCoord1x8(((pp->kind >> 16) & 0xC) + 2);
            }
            psPosition3(prev_x + ux, prev_y + uy, prev_z + uz);
            psColorRGBA4(color.r, color.g, color.b,
                       (u8) ((f32) color.a * pp->trail));
            if (pp->kind & DispTexture) {
                GXTexCoord1x8(((pp->kind >> 16) & 0xC) + 3);
            }
        GXEnd(); // Aurora requires an explicit primitive boundary.
        } else {
            f32 trail_alpha = 255.0f * (1.0f - pp->trail);
            f32 up_len = sqrtf(ux * ux + uy * uy + uz * uz);

            if (!(fabs(up_len) < FLT_MIN)) {
                f32 dy;
                f32 dx;
                f32 dz;
                f32 ratio;
                u32 num;

                dx = x - prev_x;
                dy = y - prev_y;
                dz = z - prev_z;
                dx *= dx;
                dy *= dy;
                dz *= dz;
                ratio = sqrtf(dz + (dx + dy)) / up_len;

                if (ratio < 1.0f) {
                    ratio = 1.0f;
                }
                ux *= ratio;
                uy *= ratio;
                uz *= ratio;
                num = GC_BE32(*(u32*) texform);
                texform += 4;
                for (; num != 0; num--) {
                    u8 prim = texform[0];
                    u8 count = texform[1];
                    s32 i;

                    texform += 4;
                    if (pp->kind & DispTexture) {
                        setVtxDesc(5);
                        GXBegin(prim, GX_VTXFMT5, count);
                    } else {
                        setVtxDesc(3);
                        GXBegin(prim, GX_VTXFMT3, count);
                    }
                    for (i = 0; i < count; i++) {
                        f32 s;
                        f32 t;
                        f32 sx;
                        f32 tx;
                        s32 alpha;

                        s = GC_BEF32(*(f32*) &texform[0]);
                        sx = 2.0f * (s - 0.5f);
                        if (pp->kind & TexFlipS) {
                            s = 1.0f - s;
                        }
                        t = GC_BEF32(*(f32*) &texform[4]);
                        texform += 8;
                        alpha = 255.0f - t * trail_alpha;
                        if (alpha < 0) {
                            alpha = 0;
                        }
                        if (alpha > 0xFF) {
                            alpha = 0xFF;
                        }
                        tx = 2.0f * (t - 0.5f);
                        if (pp->kind & TexFlipT) {
                            t = 1.0f - t;
                        }
                        psPosition3(ux * tx + (rx * sx + x),
                                       uy * tx + (ry * sx + y),
                                       uz * tx + (rz * sx + z));
                        psColorRGBA4(color.r, color.g, color.b, alpha);
                        if (pp->kind & DispTexture) {
                            GXTexCoord2f32(s, t);
                        }
                    }
                GXEnd(); // Aurora requires an explicit primitive boundary.
                }
            }
        }
    } else if (texform == NULL) {
        if (pp->kind & DispTexture) {
            setVtxDesc(0);
            GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        } else {
            setVtxDesc(1);
            GXBegin(GX_QUADS, GX_VTXFMT1, 4);
        }
        psPosition3(x - rx, y - ry, z - rz);
        if (pp->kind & DispTexture) {
            GXTexCoord1x8((pp->kind >> 16) & 0xC);
        }
        psPosition3(x - ux, y - uy, z - uz);
        if (pp->kind & DispTexture) {
            GXTexCoord1x8(((pp->kind >> 16) & 0xC) + 1);
        }
        psPosition3(x + rx, y + ry, z + rz);
        if (pp->kind & DispTexture) {
            GXTexCoord1x8(((pp->kind >> 16) & 0xC) + 2);
        }
        psPosition3(x + ux, y + uy, z + uz);
        if (pp->kind & DispTexture) {
            GXTexCoord1x8(((pp->kind >> 16) & 0xC) + 3);
        }
    GXEnd(); // Aurora requires an explicit primitive boundary.
    } else {
        u32 num = GC_BE32(*(u32*) texform);

        texform += 4;
        for (; num != 0; num--) {
            u8 prim = texform[0];
            u8 count = texform[1];
            s32 i;

            texform += 4;
            if (pp->kind & DispTexture) {
                setVtxDesc(4);
                GXBegin(prim, GX_VTXFMT4, count);
            } else {
                setVtxDesc(1);
                GXBegin(prim, GX_VTXFMT1, count);
            }
            for (i = 0; i < count; i++) {
                f32 s;
                f32 t;
                f32 sx;
                f32 tx;

                s = GC_BEF32(*(f32*) &texform[0]);
                sx = 2.0f * (s - 0.5f);
                if (pp->kind & TexFlipS) {
                    s = 1.0f - s;
                }
                t = GC_BEF32(*(f32*) &texform[4]);
                texform += 8;
                tx = 2.0f * (t - 0.5f);
                if (pp->kind & TexFlipT) {
                    t = 1.0f - t;
                }
                psPosition3(ux * tx + (rx * sx + x), uy * tx + (ry * sx + y),
                               uz * tx + (rz * sx + z));
                if (pp->kind & DispTexture) {
                    GXTexCoord2f32(s, t);
                }
            }
        GXEnd(); // Aurora requires an explicit primitive boundary.
        }
    }
}

PS_INLINE void psDispSub(HSD_Particle* pp, u8* texform)
{
    f32 x;
    f32 y;
    f32 z;
    f32 right_x;
    f32 up_x;
    f32 right_y;
    f32 up_y;
    f32 right_z;
    f32 up_z;
    f32 angle;
    f32 dir_x;
    f32 dir_y;
    HSD_Generator* gp;

    x = pp->pos.x;
    y = pp->pos.y;
    z = pp->pos.z;
    if (texform != NULL) {
        right_x = rvmtx[0][0] * pp->size;
        up_x = -rvmtx[0][1] * pp->size;
        right_y = rvmtx[1][0] * pp->size;
        up_y = -rvmtx[1][1] * pp->size;
        right_z = rvmtx[2][0] * pp->size;
        up_z = -rvmtx[2][1] * pp->size;
    } else {
        right_x = rx0 * pp->size;
        up_x = rx1 * pp->size;
        right_y = ry0 * pp->size;
        up_y = ry1 * pp->size;
        right_z = rz0 * pp->size;
        up_z = rz1 * pp->size;
    }
    gp = pp->gen;
    if (gp != NULL && (gp->flags & 0x20)) {
        right_x *= gp->scale.x;
        up_x *= gp->scale.x;
        right_y *= gp->scale.y;
        up_y *= gp->scale.y;
        right_z *= gp->scale.z;
        up_z *= gp->scale.z;
    }
    if ((pp->kind & Trail) || (pp->kind & DirVec)) {
        if (0.0f == prj[0]) {
            f32 prev_x;
            f32 prev_y;
            f32 prev_z;
            f32 w1inv;
            f32 w1;
            f32 w0inv;
            f32 w0;

            if (pp->kind & Tornado) {
                calcTornadoLastPos(pp, &prev_x, &prev_y, &prev_z);
            } else {
                prev_x = pp->pos.x - pp->vel.x;
                prev_y = pp->pos.y - pp->vel.y;
                prev_z = pp->pos.z - pp->vel.z;
            }
            w0 = vmtx[2][3] +
                 (GC_MADD(vmtx[2][2], pp->pos.z, (GC_MADD(vmtx[2][0], pp->pos.x, vmtx[2][1] * pp->pos.y))));
            if (0.0f == w0) {
                goto no_direction;
            }
            w0inv = -1.0f / w0;
            w1 = vmtx[2][3] + (GC_MADD(vmtx[2][2], prev_z, (GC_MADD(vmtx[2][0], prev_x, vmtx[2][1] * prev_y))));
            if (0.0f == w1) {
                goto no_direction;
            }
            w1inv = -1.0f / w1;
            dir_x = GC_MSUB(w0inv, (pvmtx[0][3] + (GC_MADD(pvmtx[0][2], pp->pos.z, (GC_MADD(pvmtx[0][0], pp->pos.x, pvmtx[0][1] * pp->pos.y))))), w1inv * (pvmtx[0][3] + (GC_MADD(pvmtx[0][2], prev_z, (GC_MADD(pvmtx[0][0], prev_x, pvmtx[0][1] * prev_y))))));
            dir_y = GC_MSUB(w0inv, (pvmtx[1][3] + (GC_MADD(pvmtx[1][2], pp->pos.z, (GC_MADD(pvmtx[1][0], pp->pos.x, pvmtx[1][1] * pp->pos.y))))), w1inv * (pvmtx[1][3] + (GC_MADD(pvmtx[1][2], prev_z, (GC_MADD(pvmtx[1][0], prev_x, pvmtx[1][1] * prev_y))))));
        } else if (pp->kind & Tornado) {
            f32 prev_x;
            f32 prev_y;
            f32 prev_z;
            f32 dx;
            f32 dy;
            f32 dz;

            calcTornadoLastPos(pp, &prev_x, &prev_y, &prev_z);
            dx = pp->pos.x - prev_x;
            dy = pp->pos.y - prev_y;
            dz = pp->pos.z - prev_z;
            dir_x = GC_MADD(pvmtx[0][2], dz, (GC_MADD(pvmtx[0][0], dx, pvmtx[0][1] * dy)));
            dir_y = GC_MADD(pvmtx[1][2], dz, (GC_MADD(pvmtx[1][0], dx, pvmtx[1][1] * dy)));
        } else {
            dir_x = GC_MADD(pvmtx[0][2], pp->vel.z, (GC_MADD(pvmtx[0][0], pp->vel.x, pvmtx[0][1] * pp->vel.y)));
            dir_y = GC_MADD(pvmtx[1][2], pp->vel.z, (GC_MADD(pvmtx[1][0], pp->vel.x, pvmtx[1][1] * pp->vel.y)));
        }
        if (fabs(dir_y) < FLT_MIN) {
            angle = (dir_x >= 0.0f) ? 1.5707964f : -1.5707964f;
        } else {
            angle = atan2fInline(dir_x, dir_y);
        }
        if (pp->kind & DirVec) {
            angle += pp->rotate;
        }
    } else {
    no_direction:
        angle = pp->rotate;
    }
    if (psDispSubAbsGtF32(angle, 0.01)) {
        Mtx mtx;
        Vec3 axis;
        f32 rx = right_x;
        f32 ry = right_y;
        f32 rz = right_z;
        f32 ux = up_x;
        f32 uy = up_y;
        f32 uz = up_z;

        axis.x = GC_MSUB(ry, uz, rz * uy);
        axis.y = GC_MSUB(rz, ux, rx * uz);
        axis.z = GC_MSUB(rx, uy, ry * ux);
        PSMTXRotAxisRad(mtx, &axis, angle);
        right_x = GC_MADD(mtx[0][2], rz, (GC_MADD(mtx[0][0], rx, mtx[0][1] * ry)));
        right_y = GC_MADD(mtx[1][2], rz, (GC_MADD(mtx[1][0], rx, mtx[1][1] * ry)));
        right_z = GC_MADD(mtx[2][2], rz, (GC_MADD(mtx[2][0], rx, mtx[2][1] * ry)));
        up_x = GC_MADD(mtx[0][2], uz, (GC_MADD(mtx[0][0], ux, mtx[0][1] * uy)));
        up_y = GC_MADD(mtx[1][2], uz, (GC_MADD(mtx[1][0], ux, mtx[1][1] * uy)));
        up_z = GC_MADD(mtx[2][2], uz, (GC_MADD(mtx[2][0], ux, mtx[2][1] * uy)));
    }
    psDispSubMakePolygon(pp, texform, x, y, z, pp->vel.x, pp->vel.y,
                         pp->vel.z, right_x, right_y, right_z, up_x, up_y,
                         up_z);
}

#define PS_FLAG_HIDDEN 0x20000000



#pragma push
#pragma optimization_level 3
static void _psDispParticlesSub(u32 target_link, u32 sw, HSD_Generator* gp, void* camera)
{
    s32 linkNo;
    u32 prevTexEdge;
    s32 alpha_compare_mode;
    u8 prevACmpParam1;
    u8 prevACmpParam2;
    s32 needs_setup;
    HSD_Particle* pp;
    u8* prevTex;
    u32 prevMirror;
    u16* prevLut;
    HSD_Tlut tlutdesc;
    GXTexObj texObj;
    HSD_Particle* sorted_particles;
    HSD_Particle* non_edge_particles;
    GXTlutObj gx_tlut_obj;
    s32 prev_tex_interp_near;
    u32 prev_kind;

    alpha_compare_mode = 0;
    prev_tex_interp_near = 0;
    prevACmpParam1 = 0;
    prevACmpParam2 = 0xFF;
    needs_setup = 1;
    for (linkNo = 0; linkNo < PS_NUM_LINK; linkNo++) {
        if (target_link & (1 << linkNo)) {
            particleSort(linkNo, &sorted_particles, &non_edge_particles);
            switch (sw) {
            case 1:
                pp = sorted_particles;
                break;
            case 2:
                pp = non_edge_particles;
                break;
            default:
                continue;
            }
            while (pp != NULL) {
                HSD_PSTexGroup* tex_group = NULL;
                HSD_PSFormGroup* form_group = NULL;
                gc_ptr32(HSD_PSFormGroup)* form_groups;
                u8* form = NULL;
                GXTexFmt fmt;
                int blend_mode;
                u8 alpha0;
                u8 alpha1;
                u32 width;
                u32 height;
                void* image;
                GXTexWrapMode wrap_s;
                f32 scale_s;
                f32 scale_t;
                GXTexWrapMode wrap_t;
                gc_ptr32(u8)* tex_table;

                if ((sw == 1) && !(pp->kind & TexEdge)) {
                    break;
                }
                if (!(pp->size < FLT_EPSILON) &&
                    (gp == NULL || pp->idnum == gp->idnum) &&
                    pp->gen->camera == camera &&
                    !(pp->kind & PS_FLAG_HIDDEN))
                {
                    if (needs_setup != 0) {
                        prevLut = NULL;
                        prevPointSize = -1;
                        prevTex = NULL;
                        prevLineWidth = -1;
                        prevChanCtrl = -1;
                        psSetupTevInvalidState();
                        prevMirror = (u32) -1;
                        prev_kind &= 0xFEFFFFFF;
                        prevTexEdge = (u32) -1;
                        HSD_FogSet(NULL);
                        prevChanMat.r = prevChanMat.g = prevChanMat.b = 0xFF;
                        prevChanAmb.r = prevChanAmb.g = prevChanAmb.b = 0xFF;
                        prevChanMat.a = prevChanAmb.a = 0xFF;
                        GXSetChanMatColor(GX_COLOR0A0, prevChanMat);
                        GXSetChanAmbColor(GX_COLOR0A0, prevChanAmb);
                        psSetupTevInvalidState();
                        psSetupTevCommon();
                        psSetColor(&prevColorPrim, 0xFF);
                        psSetColor(&prevColorEnv, 0);
                        psSetColor(&prevColorMat, 0xFF);
                        GXSetTevColor(GX_TEVREG0, prevColorPrim);
                        GXSetTevColor(GX_TEVREG1, prevColorEnv);
                        GXSetTevColor(GX_TEVREG2, prevColorMat);
                        prevBlendMode = -1;
                        GXSetZCompLoc(GX_FALSE);
                        HSD_CObjGetViewingMtx(HSD_CObjGetCurrent(), vmtx);
                        PSMTXInverse(vmtx, rvmtx);
                        {
                            if (0.0f == prj[0]) {
                                pvmtx[0][0] = GC_MADD(prj[1], vmtx[0][0], prj[2] * vmtx[2][0]);
                                pvmtx[0][1] = GC_MADD(prj[1], vmtx[0][1], prj[2] * vmtx[2][1]);
                                pvmtx[0][2] = GC_MADD(prj[1], vmtx[0][2], prj[2] * vmtx[2][2]);
                                pvmtx[0][3] = GC_MADD(prj[1], vmtx[0][3], prj[2] * vmtx[2][3]);
                                pvmtx[1][0] = GC_MADD(prj[3], vmtx[1][0], prj[4] * vmtx[2][0]);
                                pvmtx[1][1] = GC_MADD(prj[3], vmtx[1][1], prj[4] * vmtx[2][1]);
                                pvmtx[1][2] = GC_MADD(prj[3], vmtx[1][2], prj[4] * vmtx[2][2]);
                                pvmtx[1][3] = GC_MADD(prj[3], vmtx[1][3], prj[4] * vmtx[2][3]);
                            } else {
                                pvmtx[0][0] = GC_MADD(prj[1], vmtx[0][0], prj[2]);
                                pvmtx[0][1] = GC_MADD(prj[1], vmtx[0][1], prj[2]);
                                pvmtx[0][2] = GC_MADD(prj[1], vmtx[0][2], prj[2]);
                                pvmtx[0][3] = GC_MADD(prj[1], vmtx[0][3], prj[2]);
                                pvmtx[1][0] = GC_MADD(prj[3], vmtx[1][0], prj[4]);
                                pvmtx[1][1] = GC_MADD(prj[3], vmtx[1][1], prj[4]);
                                pvmtx[1][2] = GC_MADD(prj[3], vmtx[1][2], prj[4]);
                                pvmtx[1][3] = GC_MADD(prj[3], vmtx[1][3], prj[4]);
                            }
                            rx0 = rvmtx[0][0] + rvmtx[0][1];
                            rx1 = rvmtx[0][0] - rvmtx[0][1];
                            ry0 = rvmtx[1][0] + rvmtx[1][1];
                            ry1 = rvmtx[1][0] - rvmtx[1][1];
                            rz0 = rvmtx[2][0] + rvmtx[2][1];
                            rz1 = rvmtx[2][0] - rvmtx[2][1];
                        }
                        GXLoadPosMtxImm(vmtx, GX_PNMTX0);
                        {
                            Mtx billboard_mtx = { { 1.0F, 0.0F, 0.0F, 0.0F },
                                                  { 0.0F, 1.0F, 0.0F, 0.0F },
                                                  { 0.0F, 0.0F, 1.0F, 0.0F } };
                            GXLoadPosMtxImm(billboard_mtx, GX_PNMTX1);
                        }
                        currentMtx = GX_PNMTX1;
                        psSetCurrentMtx(GX_PNMTX0);
                        GXEnableTexOffsets(GX_TEXCOORD0, GX_TRUE, GX_TRUE);
                        HSD_MtxGetRotationMtx(rvmtx, rvmtxRot, 'z', 'x');
                        GXSetCullMode(GX_CULL_BACK);
                        GXSetArray(GX_VA_TEX0, billboard_tex0_u8, 2);
                        psSetupVtxFormat(GX_VTXFMT0, false, true, GX_U8);
                        psSetupVtxFormat(GX_VTXFMT1, false, false, GX_U8);
                        psSetupVtxFormat(GX_VTXFMT2, true, true, GX_U8);
                        psSetupVtxFormat(GX_VTXFMT3, true, false, GX_U8);
                        psSetupVtxFormat(GX_VTXFMT4, false, true, GX_F32);
                        psSetupVtxFormat(GX_VTXFMT5, true, true, GX_F32);
                        needs_setup = 0;
                    }

                    blend_mode = (pp->kind >> 0x16U) & 3;
                    setBlendMode(blend_mode);

                    if (pp->aCmpCount != 0) {
                        s32 scale = (65536 * pp->aCmpRemain) / pp->aCmpCount;
                        alpha0 = ((pp->aCmpParam1Target << 16) +
                                  scale * (pp->aCmpParam1 -
                                           pp->aCmpParam1Target)) >>
                                 16;
                        alpha1 = ((pp->aCmpParam2Target << 16) +
                                  scale * (pp->aCmpParam2 -
                                           pp->aCmpParam2Target)) >>
                                 16;
                    } else {
                        alpha0 = pp->aCmpParam1;
                        alpha1 = pp->aCmpParam2;
                    }
                    if ((alpha_compare_mode != pp->aCmpMode) ||
                        (prevACmpParam1 != alpha0) ||
                        (prevACmpParam2 != alpha1))
                    {
                        alpha_compare_mode = pp->aCmpMode;
                        prevACmpParam1 = alpha0;
                        prevACmpParam2 = alpha1;
                        GXSetAlphaCompare(
                            (alpha_compare_mode >> 3) & 7, prevACmpParam1,
                            (alpha_compare_mode >> 6) & 3,
                            alpha_compare_mode & 7, prevACmpParam2);
                    }

                    psSetupTev(pp);
                    setupChanCtrl(pp);
                    setupChanReg(pp);
                    setupTevReg(pp);
                    if ((pp->kind & (TexEdge | NoZComp)) != prevTexEdge) {
                        prevTexEdge = pp->kind & (TexEdge | NoZComp);
                        if (prevTexEdge & TexEdge) {
                            GXSetZMode((pp->kind & NoZComp) ? GX_FALSE : GX_TRUE,
                                       GX_LEQUAL, GX_TRUE);
                        } else {
                            GXSetZMode((pp->kind & NoZComp) ? GX_FALSE : GX_TRUE,
                                       GX_LEQUAL, GX_FALSE);
                        }
                    }
                    if (((pp->kind ^ prev_kind) & DispFog) != 0) {
                        if (pp->kind & DispFog) {
                            HSD_FogSet(particleFog);
                        } else {
                            HSD_FogSet(NULL);
                        }
                    }



                    form_groups = (gc_ptr32(HSD_PSFormGroup)*) psFormGroupArray[pp->bank];
                    if (form_groups != NULL &&
                        (form_group = GC_PTR(HSD_PSFormGroup*, form_groups[pp->texGroup])) != NULL
#ifdef MUST_MATCH
                        && form_group->formTable != NULL
#endif
                    )
                    {
                        form = GC_PTR(u8*, form_group->formTable[pp->poseNum]);
                    } else {
                        form = NULL;
                    }

                    if (pp->kind & DispTexture) {
                        if (pp->kind & MirrorS) {
                            scale_s = 2.0f;
                            wrap_s = GX_MIRROR;
                        } else {
                            scale_s = 1.0f;
                            wrap_s = GX_CLAMP;
                        }
                        if (pp->kind & MirrorT) {
                            scale_t = 2.0f;
                            wrap_t = GX_MIRROR;
                        } else {
                            scale_t = 1.0f;
                            wrap_t = GX_CLAMP;
                        }
                        if ((pp->kind & (MirrorS | MirrorT)) != prevMirror) {
                            Mtx temp_mtx;

                            prevMirror = pp->kind & (MirrorS | MirrorT);
                            prevTex = NULL;
                            PSMTXScale(temp_mtx, scale_s, scale_t, 1.0f);
                            if (pp->kind & MirrorT) {
                                temp_mtx[1][3] = 1.0f;
                            }
                            GXLoadTexMtxImm(temp_mtx, GX_TEXMTX0, GX_MTX2x4);
                            GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4,
                                              GX_TG_TEX0, GX_TEXMTX0, GX_FALSE,
                                              GX_PTIDENTITY);
                        }
                        tex_group = GC_PTR(HSD_PSTexGroup*, psTexGroupArray[pp->bank][pp->texGroup]);
                        if (tex_group != NULL) {
                            fmt = GC_BE32(tex_group->fmt);
                            tex_table = tex_group->texTable;
                            width = GC_BE32(tex_group->width);
                            height = GC_BE32(tex_group->height);
                        } else {
                            fmt = 0;
                            height = 0;
                            width = 0;
                            tex_table = NULL;
                        }
                        if (tex_table != NULL) {
                            image = GC_PTR(void*, tex_table[pp->poseNum]);
                        } else {
                            image = NULL;
                        }
                        if ((fmt == GX_TF_C4) || (fmt == GX_TF_C8)) {
                            if (tex_table != NULL) {
                                gc_ptr32(void)* palettes =
                                    (gc_ptr32(void)*) &tex_table[GC_BE32(tex_group->num)];
                                if (palettes != NULL) {
                                    if (pp->palNum != 0xFF) {
                                        tlutdesc.lut = GC_PTR(void*, palettes[pp->palNum]);
                                    } else if (!(pp->kind & ComTLUT)) {
                                        tlutdesc.lut = GC_PTR(void*, palettes[pp->poseNum]);
                                    } else {
                                        tlutdesc.lut = GC_PTR(void*, palettes[0]);
                                    }
                                    if (tlutdesc.lut != prevLut) {
                                        tlutdesc.fmt = (GXTlutFmt)
                                                           GC_BE32(tex_group->tlutfmt);
                                        tlutdesc.tlut_name = GX_TLUT0;
                                        tlutdesc.n_entries =
                                            (fmt == GX_TF_C4) ? 16 : 256;
                                        GXInitTlutObj(
                                            &gx_tlut_obj, tlutdesc.lut,
                                            tlutdesc.fmt, tlutdesc.n_entries);
                                        GXLoadTlut(&gx_tlut_obj,
                                                   tlutdesc.tlut_name);
                                    }
                                    prevTex = NULL;
                                }
                            }
                        }
                        if ((prevTex != image) && (image != NULL)) {
                            prevTex = image;
                            switch (fmt) {
                            case GX_TF_C4:
                            case GX_TF_C8:
                                GXInitTexObjCI(&texObj, image, width, height,
                                               fmt, wrap_s, wrap_t, GX_FALSE,
                                               GX_TLUT0);
                                break;
                            case GX_TF_I4:
                            case GX_TF_I8:
                            case GX_TF_IA4:
                            case GX_TF_IA8:
                            case GX_TF_RGB565:
                            case GX_TF_RGB5A3:
                            case GX_TF_RGBA8:
                            case GX_TF_CMPR:
                                GXInitTexObj(&texObj, image, width, height,
                                             fmt, wrap_s, wrap_t, GX_FALSE);
                                break;
                            default:
                                HSD_ASSERT(0x7AD, 0);
                                break;
                            }
                            prev_tex_interp_near = pp->kind & TexInterpNear;
                            GXInitTexObjLOD(
                                &texObj,
                                (prev_tex_interp_near != 0) ? GX_NEAR
                                                            : GX_LINEAR,
                                ((pp->kind >> 9) & 1) ? GX_NEAR : GX_LINEAR,
                                0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE,
                                GX_ANISO_1);
                            GXLoadTexObj(&texObj, GX_TEXMAP0);
                        }
                        if ((u32) prev_tex_interp_near !=
                            (pp->kind & TexInterpNear))
                        {
                            prev_tex_interp_near = pp->kind & TexInterpNear;
                            GXInitTexObjLOD(
                                &texObj,
                                (prev_tex_interp_near != 0) ? GX_NEAR
                                                            : GX_LINEAR,
                                (s32) (pp->kind & TexInterpNear) != 0
                                    ? GX_NEAR
                                    : GX_LINEAR,
                                0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE,
                                GX_ANISO_1);
                            GXLoadTexObj(&texObj, GX_TEXMAP0);
                        }
                    }

                    if (pp->kind & DispPoint) {
                        if (pp->appsrt != NULL) {
                            psDispSubAPPSRTPoint(pp);
                        } else if (pp->kind & Trail) {
                            pp = psDispSubPointTrail(pp);
                        } else {
                            u8 point_size;

                            psSetCurrentMtx(GX_PNMTX0);
                            point_size = (pp->size > 42.5) ? 255.0f : 6.0f * pp->size;
                            if (prevPointSize != (s32) point_size) {
                                prevPointSize = point_size;
                                GXSetPointSize(point_size, GX_TO_ONE);
                            }
                            if (pp->kind & DispTexture) {
                                setVtxDesc(0);
                                GXBegin(GX_POINTS, GX_VTXFMT0, 1);
                            } else {
                                setVtxDesc(1);
                                GXBegin(GX_POINTS, GX_VTXFMT1, 1);
                            }
                            psPosition3(pp->pos.x, pp->pos.y, pp->pos.z);
                            if (pp->kind & DispTexture) {
                                GXTexCoord1x8(1);
                            }
                        GXEnd(); // Aurora requires an explicit primitive boundary.
                        }
                    } else if (pp->appsrt != NULL) {
                        psDispSubAppSRT(pp, form);
                    } else {
                        psDispSub(pp, form);
                    }
                }

                prev_kind = pp->kind;
                pp = pp->next;
            }
        }
    }
    if (needs_setup == 0) {
        HSD_StateInvalidate(-1);
    }
}
#pragma pop

void psDispSetProjection(void* proj)
{
    memcpy(prj, proj, sizeof(prj));
}

void psDispParticles(u32 target_link, u32 sw, void* camera)
{
    _psDispParticlesSub(target_link, sw, NULL, camera);
}
