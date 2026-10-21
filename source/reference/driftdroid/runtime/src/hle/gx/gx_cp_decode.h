#pragma once

#include "gx_stream_common.h"

namespace GxCpDecode {

inline GXAttrType AttrType2(uint32_t bits) {
    return static_cast<GXAttrType>(bits & 0x3u);
}

inline bool SameVtxAttrFmt(const VtxAttrFmt& lhs, const VtxAttrFmt& rhs) {
    return lhs.cnt == rhs.cnt && lhs.type == rhs.type && lhs.frac == rhs.frac;
}

inline void VcdLo(uint32_t value) {
    bool changed = false;
    const auto assign = [&changed](GXAttrType& dst, GXAttrType next) {
        changed |= dst != next;
        dst = next;
    };
    for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX7MTXIDX; ++i) {
        assign(g_hleGxState.vtxDesc[i], (value & (1u << i)) ? GX_DIRECT : GX_NONE);
    }
    assign(g_hleGxState.vtxDesc[GX_VA_POS], AttrType2(value >> 9));
    assign(g_hleGxState.vtxDesc[GX_VA_NRM], AttrType2(value >> 11));
    assign(g_hleGxState.vtxDesc[GX_VA_CLR0], AttrType2(value >> 13));
    assign(g_hleGxState.vtxDesc[GX_VA_CLR1], AttrType2(value >> 15));
    assign(g_hleGxState.vtxDesc[GX_VA_NBT], GX_NONE);
    if (changed) {
        g_hleGxState.InvalidateVtxLayoutHash(HleGxState::kVtxLayoutDescRow);
    }
}

inline void VcdHi(uint32_t value) {
    bool changed = false;
    for (int tex = 0; tex < 8; ++tex) {
        auto& dst = g_hleGxState.vtxDesc[GX_VA_TEX0 + tex];
        const auto next = AttrType2(value >> (tex * 2));
        changed |= dst != next;
        dst = next;
    }
    if (changed) {
        g_hleGxState.InvalidateVtxLayoutHash(HleGxState::kVtxLayoutDescRow);
    }
}

inline void VatA(uint8_t fmt, uint32_t value) {
    auto& attrs = g_hleGxState.vtxAttrFmt[fmt];
    const VtxAttrFmt oldPos = attrs[GX_VA_POS];
    const VtxAttrFmt oldNrm = attrs[GX_VA_NRM];
    const VtxAttrFmt oldClr0 = attrs[GX_VA_CLR0];
    const VtxAttrFmt oldClr1 = attrs[GX_VA_CLR1];
    const VtxAttrFmt oldTex0 = attrs[GX_VA_TEX0];
    auto& pos = attrs[GX_VA_POS];
    pos.cnt = ((value >> 0) & 1u) ? GX_POS_XYZ : GX_POS_XY;
    pos.type = static_cast<GXCompType>((value >> 1) & 7u);
    pos.frac = static_cast<u8>((value >> 4) & 0x1fu);

    auto& nrm = attrs[GX_VA_NRM];
    nrm.type = static_cast<GXCompType>((value >> 10) & 7u);
    nrm.cnt = (value & 0x80000000u)
        ? GX_NRM_NBT3
        : (((value >> 9) & 1u) ? GX_NRM_NBT : GX_NRM_XYZ);
    nrm.frac = GxStream::NormalFracBits(nrm.type);

    auto& clr0 = attrs[GX_VA_CLR0];
    clr0.cnt = ((value >> 13) & 1u) ? GX_CLR_RGBA : GX_CLR_RGB;
    clr0.type = static_cast<GXCompType>((value >> 14) & 7u);
    clr0.frac = 0;

    auto& clr1 = attrs[GX_VA_CLR1];
    clr1.cnt = ((value >> 17) & 1u) ? GX_CLR_RGBA : GX_CLR_RGB;
    clr1.type = static_cast<GXCompType>((value >> 18) & 7u);
    clr1.frac = 0;

    auto& tex0 = attrs[GX_VA_TEX0];
    tex0.cnt = ((value >> 21) & 1u) ? GX_TEX_ST : GX_TEX_S;
    tex0.type = static_cast<GXCompType>((value >> 22) & 7u);
    tex0.frac = static_cast<u8>((value >> 25) & 0x1fu);
    if (!SameVtxAttrFmt(oldPos, pos) || !SameVtxAttrFmt(oldNrm, nrm) ||
        !SameVtxAttrFmt(oldClr0, clr0) || !SameVtxAttrFmt(oldClr1, clr1) ||
        !SameVtxAttrFmt(oldTex0, tex0)) {
        g_hleGxState.InvalidateVtxLayoutHash(HleGxState::VtxLayoutFmtRow(fmt));
    }
}

inline void VatB(uint8_t fmt, uint32_t value) {
    auto& attrs = g_hleGxState.vtxAttrFmt[fmt];
    VtxAttrFmt before[4];
    for (int tex = 1; tex <= 4; ++tex) before[tex - 1] = attrs[GX_VA_TEX0 + tex];
    for (int tex = 1; tex <= 4; ++tex) {
        const int shift = (tex - 1) * 9;
        auto& attr = attrs[GX_VA_TEX0 + tex];
        attr.cnt = ((value >> shift) & 1u) ? GX_TEX_ST : GX_TEX_S;
        attr.type = static_cast<GXCompType>((value >> (shift + 1)) & 7u);
        if (tex != 4) {
            attr.frac = static_cast<u8>((value >> (shift + 4)) & 0x1fu);
        }
    }
    bool changed = false;
    for (int tex = 1; tex <= 4; ++tex) changed |= !SameVtxAttrFmt(before[tex - 1], attrs[GX_VA_TEX0 + tex]);
    if (changed) {
        g_hleGxState.InvalidateVtxLayoutHash(HleGxState::VtxLayoutFmtRow(fmt));
    }
}

inline void VatC(uint8_t fmt, uint32_t value) {
    auto& attrs = g_hleGxState.vtxAttrFmt[fmt];
    VtxAttrFmt before[4];
    for (int tex = 4; tex <= 7; ++tex) before[tex - 4] = attrs[GX_VA_TEX0 + tex];
    attrs[GX_VA_TEX4].frac = static_cast<u8>(value & 0x1fu);
    for (int tex = 5; tex <= 7; ++tex) {
        const int shift = 5 + (tex - 5) * 9;
        auto& attr = attrs[GX_VA_TEX0 + tex];
        attr.cnt = ((value >> shift) & 1u) ? GX_TEX_ST : GX_TEX_S;
        attr.type = static_cast<GXCompType>((value >> (shift + 1)) & 7u);
        attr.frac = static_cast<u8>((value >> (shift + 4)) & 0x1fu);
    }
    bool changed = false;
    for (int tex = 4; tex <= 7; ++tex) changed |= !SameVtxAttrFmt(before[tex - 4], attrs[GX_VA_TEX0 + tex]);
    if (changed) {
        g_hleGxState.InvalidateVtxLayoutHash(HleGxState::VtxLayoutFmtRow(fmt));
    }
}

// Applies one CP register write, wherever it came from: a GX_LOAD_CP_REG packet
// in the raw FIFO stream, the same packet inside a display list, or a replay of
// a display list's recorded CP writes on a scan-cache hit. Registers this does
// not decode (0x30 draw-sync/matrix-index config and 0x40 among them) are
// dropped, identically for every caller.
inline void ApplyCpRegWrite(uint8_t reg, uint32_t value) {
    if (reg == 0x50) {
        VcdLo(value);
        return;
    }
    if (reg == 0x60) {
        VcdHi(value);
        return;
    }
    if (reg >= 0x70 && reg <= 0x77) {
        VatA(static_cast<uint8_t>(reg - 0x70), value);
        return;
    }
    if (reg >= 0x80 && reg <= 0x87) {
        VatB(static_cast<uint8_t>(reg - 0x80), value);
        return;
    }
    if (reg >= 0x90 && reg <= 0x97) {
        VatC(static_cast<uint8_t>(reg - 0x90), value);
        return;
    }
    if (reg >= 0xA0 && reg <= 0xAF) {
        const int attr = (reg - 0xA0) + GX_VA_POS;
        if (attr >= 0 && attr < 26) {
            g_hleGxState.vtxArray[attr].base = DecodeCpArrayBaseGuestAddress(value);
        }
        return;
    }
    if (reg >= 0xB0 && reg <= 0xBF) {
        const int attr = (reg - 0xB0) + GX_VA_POS;
        if (attr >= 0 && attr < 26) {
            g_hleGxState.vtxArray[attr].stride = value;
        }
        return;
    }
}

} // namespace GxCpDecode
