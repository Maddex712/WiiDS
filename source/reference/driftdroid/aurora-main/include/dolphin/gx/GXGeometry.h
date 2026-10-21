#ifndef DOLPHIN_GXGEOMETRY_H
#define DOLPHIN_GXGEOMETRY_H

#include <dolphin/gx/GXEnum.h>

#ifdef __cplusplus
extern "C" {
#endif

void GXSetVtxDesc(GXAttr attr, GXAttrType type);
void GXSetSourceVtxDesc(GXAttr attr, GXAttrType type);
void GXSetVtxDescv(GXVtxDescList* list);
void GXClearVtxDesc(void);
void GXSetVtxAttrFmtv(GXVtxFmt vtxfmt, const GXVtxAttrFmtList* list);
void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac);
void GXSetNumTexGens(u8 nTexGens);
void GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts);
void GXSetTexCoordGen2(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u32 mtx, GXBool normalize,
                       u32 postmtx);
void GXSetLineWidth(u8 width, GXTexOffset texOffsets);
void GXSetPointSize(u8 pointSize, GXTexOffset texOffsets);
void GXEnableTexOffsets(GXTexCoordID coord, GXBool line_enable, GXBool point_enable);
#ifdef TARGET_PC
// A single extern "C" declaration with a default argument, not two overloaded declarations of
// the same name - C linkage does not support overloading (two same-named extern "C" decls with
// different signatures is a genuine conflict), which GCC (devkitA64/Switch) correctly rejects but
// Clang (every other platform this project has targeted) has historically been more lenient
// about. A default argument is resolved at the call site in C++ source, not part of the C ABI,
// so it carries no such restriction - real 4-arg callers (e.g. runtime/src/hle/gx/gx_dl.cpp) and
// explicit 5-arg callers both keep working unchanged.
void GXSetArray(GXAttr attr, const void* data, u32 size, u8 stride, bool le = false);
#define GXSETARRAY(attr, data, size, stride, le) GXSetArray((attr), (data), (size), (stride), (le))
#else
void GXSetArray(GXAttr attr, const void* data, u8 stride);
#define GXSETARRAY(attr, data, size, stride, le) GXSetArray((attr), (data), (stride))
#endif
void GXInvalidateVtxCache(void);

static inline void GXSetTexCoordGen(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u32 mtx) {
  GXSetTexCoordGen2(dst_coord, func, src_param, mtx, GX_FALSE, GX_PTIDENTITY);
}

#ifdef __cplusplus
}
#endif

#endif
