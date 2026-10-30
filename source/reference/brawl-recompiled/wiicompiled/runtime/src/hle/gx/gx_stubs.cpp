#include <algorithm>
#include "gx_internal.h"
#include "runtime_log.h"

extern "C" void __GXSetSUTexRegs();

// ============================================================================
// FIFO Write Helpers
// ============================================================================

extern "C" void GX_HLE_FIFO_WriteFloat(float val) {
    u32 raw; std::memcpy(&raw, &val, 4);
    try { HleFifoWrite(raw, 4); } catch (...) { RT_LOGF(RT_TAG_GX, "FIFO write float failed\n"); }
}

extern "C" void GX_HLE_FIFO_Write32(uint32_t val) { HleFifoWrite(val, 4); }
extern "C" void GX_HLE_FIFO_Write16(uint16_t val) { HleFifoWrite(static_cast<u32>(val), 2); }
extern "C" void GX_HLE_FIFO_Write8(uint8_t val) { HleFifoWrite(static_cast<u32>(val), 1); }

extern "C" void GX__SetDrawSync_8016ed08(uint32_t token) {
    (void)token;
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) {
        if (Memory::Read32(gd + 0x5FCu)) GX__SetDirtyState_8016ee78();
        Memory::Write16(gd + 2, 0);
    } } catch (...) {}
}

extern "C" void GX__SetDrawSync_8016e9fc(uint32_t token) { GX__SetDrawSync_8016ed08(token); }

// SPM GXSetDrawSync (0x8028cc90). sysWaitDrawSync polls the PE token register through __peReg for up to
// 100 ms; no PE is emulated and __peReg was 0, so every wait timed out (3.5 s frames when the Blooper's
// offscreen NPCs draw, ta2_02). Draws are consumed in order here, so the token is reached at once: point
// __peReg at a RAM block and store the token in its token register (+0xE).
extern "C" void SPM_GXSetDrawSync(uint32_t token) {
    constexpr uint32_t kSpmPeRegPtrAddr = 0x805A0878u;  // __peReg (SPM: r13-0x6FB0 in sysWaitDrawSync)
    constexpr uint32_t kSpmPeRegBlock = 0x80002F00u;    // low-memory debugger area; no exception vectors are installed
    try {
        Memory::Write32(kSpmPeRegPtrAddr, kSpmPeRegBlock);
        Memory::Write16(kSpmPeRegBlock + 0xEu, static_cast<uint16_t>(token));
    } catch (const ::Memory::AccessViolation&) {}
}
PPC_NATIVE_OVERRIDE_VOID(801f08d8, SPM_GXSetDrawSync, (uint32_t token), (token));
#if 0  // NSMBW: GXSetDrawSync is not linked into NSMBW (was MKW-only)
#if 0  // SPM: 0x8016e9fc: no NSMBW symbol at this address
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE_VOID(8016e9fc, GX__SetDrawSync_8016e9fc, (uint32_t token), (token));
#endif
#endif
#endif

extern "C" void OSWakeupThread_HLE_801aaaa4(CpuContext* ctx);

// Brawl (RSBE01) GXFinishInterruptHandler globals: DrawDoneCB r13-0x3B5C, FinishQueue r13-0x3B68.
constexpr uint32_t kGxDrawDoneCallbackAddr = 0x805A08C4u;
constexpr uint32_t kGxFinishQueueAddr = 0x805A08B8u;

extern "C" void GX__FinishInterruptHandler_8016ed94() {
    try {
        uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) Memory::Write16(gd + 0x0Au, static_cast<uint16_t>(Memory::Read16(gd + 0x0Au) | 0x0008u));
        Memory::Write8(kGxDrawDoneFlagAddr, 1);
    } catch (...) {}
    // As the SDK handler: the game's draw-done callback, then wake GXWaitDrawDone sleepers. Brawl's frame
    // pacing (gfApplication's frame buffer slots) is released from that callback.
    if (CpuContext* cpu = TryGetCpuContext()) {
        if (const uint32_t cb = Memory::Read32(kGxDrawDoneCallbackAddr)) {
            CpuContext call = *cpu;
            InvokeIndirectCpu(cb, &call);
        }
        CpuContext wake = *cpu;
        wake.gpr[3] = kGxFinishQueueAddr;
        OSWakeupThread_HLE_801aaaa4(&wake);
    }
}
PPC_NATIVE_OVERRIDE_VOID(801f0d2c, GX__FinishInterruptHandler_8016ed94, (), ());

extern "C" void GX__DrawDone_8016eab0() {
    try { Memory::Write8(kGxDrawDoneFlagAddr, 0); } catch (...) {}
    GXDrawDone(); GX__FinishInterruptHandler_8016ed94();
}
PPC_NATIVE_OVERRIDE_VOID(801f0a30, GX__DrawDone_8016eab0, (), ());

extern "C" void GX__PixModeSync_8016eb70() {
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write16(gd + 2, 0); } catch (...) {}
    GXPixModeSync();
}
PPC_NATIVE_OVERRIDE_VOID(801f0af0, GX__PixModeSync_8016eb70, (), ());

// ============================================================================
// Hardware Revision / Thread Query - No-ops
// ============================================================================

extern "C" void __GX__InitRevisionBits_8016b720() {}
PPC_NATIVE_OVERRIDE_VOID(801ed7d8, __GX__InitRevisionBits_8016b720, (), ());

// ============================================================================
// Texture State Management - Aurora handles internally
// ============================================================================

extern "C" void __GX__SetSUTexRegs_801712f0() {
    __GXSetSUTexRegs();
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write16(gd + 2, 0); } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(801f3294, __GX__SetSUTexRegs_801712f0, (), ());

extern "C" void __GX__SetTmemConfig_80171458(uint32_t mode) {
    // TMEM layout configuration - Aurora manages internally
    (void)mode;
}
PPC_NATIVE_OVERRIDE_VOID(801F33FC, __GX__SetTmemConfig_80171458, (uint32_t mode), (mode));

extern "C" void __GX__FlushTextureState_80171c28() {
    // BP texture state flush - Aurora handles via API
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write16(gd + 2, 0); } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(801f3b78, __GX__FlushTextureState_80171c28, (), ());

// ============================================================================
// Copy Configuration - No-ops for features Aurora doesn't use
// ============================================================================

extern "C" void GX__SetDispCopyFrame2Field_8016f5f8(uint32_t f) {
    GXSetDispCopyFrame2Field(f);
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            Memory::Write32(gd + 0x23Cu, (Memory::Read32(gd + 0x23Cu) & 0xFFFFCFFFu) | ((f & 3u) << 12));
            Memory::Write32(gd + 0x24Cu, Memory::Read32(gd + 0x24Cu) & 0xFFFFCFFFu);
        }
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(801f16e4, GX__SetDispCopyFrame2Field_8016f5f8, (uint32_t f), (f));

extern "C" void GX__SetCopyClamp_8016f618(uint32_t c) {
    GXSetCopyClamp(static_cast<GXFBClamp>(c));
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            const uint32_t clamp = c & 3u;
            Memory::Write32(gd + 0x23Cu, (Memory::Read32(gd + 0x23Cu) & 0xFFFFFFFCu) | clamp);
            Memory::Write32(gd + 0x24Cu, (Memory::Read32(gd + 0x24Cu) & 0xFFFFFFFCu) | clamp);
        }
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(801f1704, GX__SetCopyClamp_8016f618, (uint32_t c), (c));

extern "C" void GX__ClearBoundingBox_8016fecc() {
    SpmBBoxClear();
    GXClearBoundingBox();
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) Memory::Write16(gd + 2, 0);
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(801f1fb8, GX__ClearBoundingBox_8016fecc, (), ());

// GXReadBoundingBox (SPM 0x8028E26C, this SDK's order: left, top, right, bottom).
extern "C" void GX__ReadBoundingBox_SPM(uint32_t leftPtr, uint32_t topPtr, uint32_t rightPtr, uint32_t bottomPtr) {
    uint16_t l = 0, t = 0, r = 0, b = 0;
    SpmBBoxRead(l, t, r, b);
    {   // union with Aurora's CPU-projected box, which also sees display-list and FIFO draws
        u16 al = 1023, ar = 0, at = 1023, ab = 0;
        ::GXReadBoundingBox(&al, &ar, &at, &ab);
        if (al <= ar && at <= ab) {
            if (l > r || t > b) { l = al; r = ar; t = at; b = ab; }
            else { l = std::min<uint16_t>(l, al); r = std::max<uint16_t>(r, ar); t = std::min<uint16_t>(t, at); b = std::max<uint16_t>(b, ab); }
        }
    }
    try {
        if (leftPtr) Memory::Write16(leftPtr, l);
        if (topPtr) Memory::Write16(topPtr, t);
        if (rightPtr) Memory::Write16(rightPtr, r);
        if (bottomPtr) Memory::Write16(bottomPtr, b);
    } catch (const Memory::AccessViolation&) {
    }
}
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE_VOID(8028E26C, GX__ReadBoundingBox_SPM,
                         (uint32_t leftPtr, uint32_t topPtr, uint32_t rightPtr, uint32_t bottomPtr),
                         (leftPtr, topPtr, rightPtr, bottomPtr));
#endif

// ============================================================================
// FIFO/State Management - No-ops
// ============================================================================

extern "C" void GX__SetDirtyState_8016ee78() {
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write32(gd + 0x5FCu, 0); } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(801f0e10, GX__SetDirtyState_8016ee78, (), ());

extern "C" void GX__ResetWriteGatherPipe_8016e6b0() {
    // WPAR reset - not needed on host
}
PPC_NATIVE_OVERRIDE_VOID(801f058c, GX__ResetWriteGatherPipe_8016e6b0, (), ());
