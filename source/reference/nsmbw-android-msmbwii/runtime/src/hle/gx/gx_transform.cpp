// gx_transform.cpp - Viewport, Projection, and Matrix Functions
#include "isa/big_endian.h"
#include "gx_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace {
    static inline void SwapBeF32ArrayToHost(const uint32_t* srcBe, float* dst, size_t count) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(srcBe);
        for (size_t i = 0; i < count; ++i) {
            dst[i] = BigEndian::ReadFloat32(bytes + i * sizeof(uint32_t));
        }
    }

    static void UpdateProjectionVectorFromMatrix(const float* mtxRowMajor, GXProjectionType projType) {
        g_projectionVector[0] = (projType == GX_PERSPECTIVE) ? 0.0f : 1.0f;
        g_projectionVector[1] = mtxRowMajor[0];
        g_projectionVector[3] = mtxRowMajor[5];
        g_projectionVector[5] = mtxRowMajor[10];
        g_projectionVector[6] = mtxRowMajor[11];
        if (projType == GX_PERSPECTIVE) {
            g_projectionVector[2] = mtxRowMajor[2];
            g_projectionVector[4] = mtxRowMajor[6];
        } else {
            g_projectionVector[2] = mtxRowMajor[3];
            g_projectionVector[4] = mtxRowMajor[7];
        }
    }

    static void UpdateProjectionVectorFromProjV(const float* projV) {
        std::memcpy(g_projectionVector, projV, sizeof(g_projectionVector));
    }

}

// ============================================================================
// Viewport
// ============================================================================

// Viewport and scissor go to aurora in guest EFB coordinates, mapped to the active render target
// exactly once inside gx::map_logical_viewport/map_logical_scissor. A prior VI origin/scale
// transform double-transformed menu viewports and broke 640x480 overscan, so it was removed.
extern "C" void GX__SetViewportJitter_80173378(float l, float t, float w, float h, float nz, float fz, uint32_t f) {
    g_viewportState[0]=l; g_viewportState[1]=t; g_viewportState[2]=w; g_viewportState[3]=h; g_viewportState[4]=nz; g_viewportState[5]=fz;
    GXSetViewportJitter(l, t, w, h, nz, fz, f);
}
PPC_NATIVE_OVERRIDE_VOID(801C9BD0, GX__SetViewportJitter_80173378, (float l, float t, float w, float h, float nz, float fz, uint32_t f), (l, t, w, h, nz, fz, f));

extern "C" void GX__SetViewport_801733b4(float l, float t, float w, float h, float nz, float fz) {
    g_viewportState[0]=l; g_viewportState[1]=t; g_viewportState[2]=w; g_viewportState[3]=h; g_viewportState[4]=nz; g_viewportState[5]=fz;
    GXSetViewport(l, t, w, h, nz, fz);
}
PPC_NATIVE_OVERRIDE_VOID(801c9c10, GX__SetViewport_801733b4, (float l, float t, float w, float h, float nz, float fz), (l, t, w, h, nz, fz));

extern "C" void GX__GetViewportv_801733e0(uint32_t oa) {
    // EGG::StateGX::GXSetViewport (0x802418D0) reads the current viewport unconditionally, and
    // every offscreen bake reaches it via its clear pass (0x802D1900) before building its own
    // projection, so arming the frustum-scale bypass here always beats the bake, including
    // RaceScene::createSubsystems's frame-boundary-less ones. Gated to one walk per GX frame to
    // skip the cross-TU call on repeats, while the first viewport read of each frame stays unthrottled.
    static int lastBypassFrame = -1;
    if (lastBypassFrame != g_gxFrameCount) {
        lastBypassFrame = g_gxFrameCount;
        AssertMkwOffscreenScreenBypass();
    }
    if (!oa) return; for(int i=0; i<6; ++i) WriteGuestFloat(oa + i*4, g_viewportState[i]);
}
PPC_NATIVE_OVERRIDE_VOID(801c9c40, GX__GetViewportv_801733e0, (uint32_t oa), (oa));

extern "C" void GX__SetZScaleOffset_80173400(float s, float o) {
    GXSetZScaleOffset(s, o);
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            constexpr float kZ24Scale = 16777215.0f;
            Memory::WriteFloat32(gd + 0x55Cu, kZ24Scale * o);
            Memory::WriteFloat32(gd + 0x560u, 1.0f + kZ24Scale * s);
            Memory::Write32(gd + 0x5FCu, Memory::Read32(gd + 0x5FCu) | 0x10000000u);
        }
    } catch (...) {}
}
#if 0  // NSMBW: GXSetZScaleOffset is not linked into NSMBW (was MKW-only)
PPC_NATIVE_OVERRIDE_VOID(80173400, GX__SetZScaleOffset_80173400, (float s, float o), (s, o));
#endif

extern "C" void GX__SetScissorBoxOffset_801734e0(int32_t xo, int32_t yo) {
    GXSetScissorBoxOffset(xo, yo);
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) Memory::Write16(gd + 2, 0);
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(801c9d20, GX__SetScissorBoxOffset_801734e0, (int32_t xo, int32_t yo), (xo, yo));

// ============================================================================
// Scissor
// ============================================================================

extern "C" void GX__SetScissor_80173430(uint32_t l, uint32_t t, uint32_t w, uint32_t h) {
    g_scissorLeft=(int32_t)l; g_scissorTop=(int32_t)t; g_scissorWidth=(int32_t)w; g_scissorHeight=(int32_t)h;
    try { uint32_t gd=Memory::Read32(kGXDataPtrAddr); if(gd){
        uint32_t r148=Memory::Read32(gd+0x148), r14c=Memory::Read32(gd+0x14c);
        uint32_t sx=g_scissorLeft+0x156, sy=g_scissorTop+0x156, ex=sx+g_scissorWidth-1, ey=sy+g_scissorHeight-1;
        Memory::Write32(gd+0x148, ((sx<<12)&0x7ff000u)|(sy&0x7ffu)|(r148&0xff800800u));
        Memory::Write32(gd+0x14c, ((ex<<12)&0x7ff000u)|(ey&0x7ffu)|(r14c&0xff800800u));
        Memory::Write16(gd+2, 0);
    } } catch(...) {}
    // No viewport replay here: aurora recomputes viewport and scissor together
    // on every scissor change (set_logical_scissor -> apply_logical_render_state),
    // so re-issuing the current viewport would be duplicate work.
    GXSetScissor(l, t, w, h);
}
PPC_NATIVE_OVERRIDE_VOID(801C9C60, GX__SetScissor_80173430, (uint32_t l, uint32_t t, uint32_t w, uint32_t h), (l, t, w, h));

// ============================================================================
// Projection
// ============================================================================

extern "C" void GX__SetProjection_8017301c(uint32_t ma, uint32_t pt) {
    const uint32_t* raw=(const uint32_t*)GuestToHostPtr(ma, 64); float m[16];
    SwapBeF32ArrayToHost(raw, m, 16);
    // NSMBW_LOG_ORTHO: each distinct orthographic projection's extent (finding the 2D layer's), and
    // each perspective one's aspect (x scale against y scale).
    static const bool logOrtho = std::getenv("NSMBW_LOG_ORTHO") != nullptr;
    if (logOrtho && pt == GX_PERSPECTIVE && m[0] != 0.f) {
        static std::vector<int> seenAspects;
        const int key = static_cast<int>(m[5] / m[0] * 1000.f);
        if (std::find(seenAspects.begin(), seenAspects.end(), key) == seenAspects.end() && seenAspects.size() < 32) {
            seenAspects.push_back(key);
            const CpuContext* cpu = TryGetCpuContext();
            uint32_t back = 0, ra = 0;
            if (cpu != nullptr && Memory::TryRead32(cpu->gpr[1], back) && back != 0) {
                Memory::TryRead32(back + 4, ra);
            }
            std::fprintf(stderr, "[gx] perspective aspect %.3f lr %08X %08X\n", m[5] / m[0], cpu ? cpu->lr : 0, ra);
        }
    }
    if (logOrtho && pt == GX_ORTHOGRAPHIC && m[0] != 0.f && m[5] != 0.f) {
        static std::vector<std::pair<int, int>> seen;
        const std::pair<int, int> key{static_cast<int>(2.f / m[0]), static_cast<int>(2.f / m[5])};
        if (std::find(seen.begin(), seen.end(), key) == seen.end() && seen.size() < 64) {
            seen.push_back(key);
            char chain[96] = {};
            if (const CpuContext* cpu = TryGetCpuContext()) {
                uint32_t sp = cpu->gpr[1];
                int len = std::snprintf(chain, sizeof(chain), "lr %08X", cpu->lr);
                for (int depth = 0; depth < 5 && sp != 0 && len < 80; ++depth) {
                    uint32_t back = 0, ra = 0;
                    if (!Memory::TryRead32(sp, back) || back == 0 || !Memory::TryRead32(back + 4, ra)) {
                        break;
                    }
                    len += std::snprintf(chain + len, sizeof(chain) - len, " %08X", ra);
                    sp = back;
                }
            }
            std::fprintf(stderr, "[gx] ortho %d x %d (offset %.1f, %.1f) %s\n", key.first, key.second, -m[3] / m[0],
                         -m[7] / m[5], chain);
        }
    }
    GXSetProjection(m, (GXProjectionType)pt);
    UpdateProjectionVectorFromMatrix(m, (GXProjectionType)pt);
}
PPC_NATIVE_OVERRIDE_VOID(801c9840, GX__SetProjection_8017301c, (uint32_t ma, uint32_t pt), (ma, pt));

extern "C" void GX__SetProjectionv_80173080(uint32_t pa) {
    const uint32_t* raw=(const uint32_t*)GuestToHostPtr(pa, 28); float v[7];
    SwapBeF32ArrayToHost(raw, v, 7);
    GXProjectionType pt=(v[0]!=0.f)?GX_ORTHOGRAPHIC:GX_PERSPECTIVE;
    static const bool logOrthoV = std::getenv("NSMBW_LOG_ORTHO") != nullptr;
    if (logOrthoV && pt == GX_PERSPECTIVE && v[1] != 0.f) {
        static std::vector<int> seenAspects;
        const int key = static_cast<int>(v[3] / v[1] * 1000.f);
        if (std::find(seenAspects.begin(), seenAspects.end(), key) == seenAspects.end() && seenAspects.size() < 32) {
            seenAspects.push_back(key);
            const CpuContext* cpu = TryGetCpuContext();
            uint32_t chainSp = cpu ? cpu->gpr[1] : 0, ras[4] = {};
            for (int d = 0; d < 4 && chainSp != 0; ++d) {
                uint32_t back = 0;
                if (!Memory::TryRead32(chainSp, back) || back == 0) break;
                Memory::TryRead32(back + 4, ras[d]);
                chainSp = back;
            }
            std::fprintf(stderr, "[gx] perspective aspect %.3f lr %08X %08X %08X %08X %08X\n", v[3] / v[1],
                         cpu ? cpu->lr : 0, ras[0], ras[1], ras[2], ras[3]);
        }
    }
    if (logOrthoV && pt == GX_ORTHOGRAPHIC && v[1] != 0.f && v[3] != 0.f) {
        static std::vector<std::pair<int, int>> seen;
        const std::pair<int, int> key{static_cast<int>(2.f / v[1]), static_cast<int>(2.f / v[3])};
        if (std::find(seen.begin(), seen.end(), key) == seen.end() && seen.size() < 64) {
            seen.push_back(key);
            char chain[96] = {};
            if (const CpuContext* cpu = TryGetCpuContext()) {
                uint32_t sp = cpu->gpr[1];
                int len = std::snprintf(chain, sizeof(chain), "lr %08X", cpu->lr);
                for (int depth = 0; depth < 5 && sp != 0 && len < 80; ++depth) {
                    uint32_t back = 0, ra = 0;
                    if (!Memory::TryRead32(sp, back) || back == 0 || !Memory::TryRead32(back + 4, ra)) {
                        break;
                    }
                    len += std::snprintf(chain + len, sizeof(chain) - len, " %08X", ra);
                    sp = back;
                }
            }
            std::fprintf(stderr, "[gx] orthov %d x %d (offset %.1f, %.1f) %s\n", key.first, key.second,
                         -v[2] / v[1], -v[4] / v[3], chain);
        }
    }
    float m[16]={0.f}; m[0]=v[1]; m[5]=v[3]; m[10]=v[5]; m[11]=v[6];
    if(pt==GX_PERSPECTIVE){ m[2]=v[2]; m[6]=v[4]; m[14]=-1.f; } else { m[3]=v[2]; m[7]=v[4]; m[15]=1.f; }
    GXSetProjection(m, pt);
    UpdateProjectionVectorFromProjV(v);
}
PPC_NATIVE_OVERRIDE_VOID(801C98B0, GX__SetProjectionv_80173080, (uint32_t pa), (pa));

extern "C" void GX__GetProjectionv_801730cc(uint32_t pa) {
    uint8_t* out = static_cast<uint8_t*>(GuestToHostPtr(pa, 28));
    for (int i = 0; i < 7; ++i) {
        BigEndian::WriteFloat32(out + i * sizeof(float), g_projectionVector[i]);
    }
}
PPC_NATIVE_OVERRIDE_VOID(801c9900, GX__GetProjectionv_801730cc, (uint32_t pa), (pa));

// ============================================================================
// Matrix Loading
// ============================================================================

extern "C" void GX__LoadPosMtxImm_8017310c(uint32_t ma, uint32_t id) {
    const uint32_t* raw=(const uint32_t*)GuestToHostPtr(ma); float m[12];
    SwapBeF32ArrayToHost(raw, m, 12);
    GXLoadPosMtxImm((float(*)[4])m, id);
}
PPC_NATIVE_OVERRIDE_VOID(801c9940, GX__LoadPosMtxImm_8017310c, (uint32_t ma, uint32_t id), (ma, id));

extern "C" void GX__LoadPosMtxIndx_8017315c(uint32_t mi, uint32_t id) {
    const auto& arr=g_hleGxState.vtxArray[GX_POS_MTX_ARRAY];
    if(arr.base==0||arr.stride==0) return;
    const uint32_t* raw=(const uint32_t*)GuestToHostPtr(arr.base+mi*arr.stride, 48);
    if(raw){ float m[12]; SwapBeF32ArrayToHost(raw,m,12); GXLoadPosMtxImm((float(*)[4])m,id); }
}
PPC_NATIVE_OVERRIDE_VOID(801c9990, GX__LoadPosMtxIndx_8017315c, (uint32_t mi, uint32_t id), (mi, id));

extern "C" void GX__LoadNrmMtxImm_80173188(uint32_t ma, uint32_t id) {
    const uint32_t* raw=(const uint32_t*)GuestToHostPtr(ma, 48); float m[12];
    SwapBeF32ArrayToHost(raw, m, 12); GXLoadNrmMtxImm((float(*)[4])m, id);
}
PPC_NATIVE_OVERRIDE_VOID(801C99C0, GX__LoadNrmMtxImm_80173188, (uint32_t ma, uint32_t id), (ma, id));

extern "C" void GX__LoadNrmMtxIndx3x3_801731e0(uint32_t mi, uint32_t id) {
    const auto& arr=g_hleGxState.vtxArray[GX_NRM_MTX_ARRAY];
    if(arr.base==0||arr.stride==0) return;
    const uint32_t* raw=(const uint32_t*)GuestToHostPtr(arr.base+mi*arr.stride, 36);
    if(raw){
        float src[9]{};
        float m[12]{};
        SwapBeF32ArrayToHost(raw, src, 9);
        for (uint32_t row = 0; row < 3; ++row) {
            for (uint32_t col = 0; col < 3; ++col) {
                m[row * 4 + col] = src[row * 3 + col];
            }
        }
        GXLoadNrmMtxImm((float(*)[4])m, id);
    }
}
PPC_NATIVE_OVERRIDE_VOID(801c9a20, GX__LoadNrmMtxIndx3x3_801731e0, (uint32_t mi, uint32_t id), (mi, id));

extern "C" void GX__LoadTexMtxImm_80173234(uint32_t ma, uint32_t id, uint32_t t) {
    size_t c=(t==(uint32_t)GX_MTX3x4)?12:8;
    const uint32_t* raw=(const uint32_t*)GuestToHostPtr(ma,c*4); float l[12]={};
    SwapBeF32ArrayToHost(raw,l,c); GXLoadTexMtxImm(l, id, (GXTexMtxType)t);
}
PPC_NATIVE_OVERRIDE_VOID(801C9A80, GX__LoadTexMtxImm_80173234, (uint32_t ma, uint32_t id, uint32_t t), (ma, id, t));

extern "C" void GX__SetCurrentMtx_80173214(uint32_t id) { GXSetCurrentMtx(id); }
PPC_NATIVE_OVERRIDE_VOID(801C9A60, GX__SetCurrentMtx_80173214, (uint32_t id), (id));
