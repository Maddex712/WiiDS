// Native THP video/audio decode, replacing the guest's own decoder.
//
// THP is essentially MJPEG, and the guest decoder is the single most expensive thing in the game
// outside a race: profiles put the Select Cup screen at 9 FPS with >55% of the frame inside
// __THPInverseDCTNoYPos / __THPInverseDCTY8 / __THPHuffDecodeDCTComp*, and the attract-mode demo
// at ~72% THP decode. Running it as translated PPC costs roughly six ARM64 instructions per guest
// instruction, so a native decoder is worth far more here than anywhere else in the game.
//
// The decoder itself is aurora's (upstream commit 3251f4e2, "Add aurora::thp implementation"),
// which upstream verified bit-exact against Dolphin for both THPVideoDecode and THPAudioDecode.
// This file is only the guest-ABI shim: guest addresses in, host pointers out.

#include "hle_stubs.h"
#include "memory_access.h"
#include "runtime_log.h"

#include <dolphin/thp.h>

#include <cstdint>

namespace {

// The guest hands us MEM1/MEM2 addresses. Sizes are not known up front (the frame header carries
// them), so map a single byte and rely on the decoder staying inside the buffer the game sized -
// the same contract the translated code had.
void* GuestPtr(uint32_t addr) {
    if (addr == 0) {
        return nullptr;
    }
    try {
        return Memory::GetPointer(addr, 1);
    } catch (const Memory::AccessViolation& e) {
        LogMemoryError(RT_TAG_RUNTIME, "THP guest pointer", e);
        return nullptr;
    }
}

// Bytes one decoded plane occupies: aurora writes 8x4 I8 tiles, a whole tile per partial edge.
uint32_t TiledPlaneBytes(uint32_t width, uint32_t height) {
    return ((width + 7u) / 8u) * ((height + 3u) / 4u) * 32u;
}

} // namespace

// Defined in gx_objects.cpp: the entry point for host writes into guest RAM that no DC flush covers.
extern "C" void GxNotifyGuestRamDmaWrite(uint32_t addr, uint32_t size);

extern "C" uint32_t THP__VideoDecode_801b3bac(uint32_t fileAddr, uint32_t tileYAddr, uint32_t tileUAddr,
                                              uint32_t tileVAddr, uint32_t workAddr) {
    // Pass nulls straight through rather than short-circuiting: aurora returns kNoInput for a null
    // file and kNoOutput for a null tile, whereas returning 0 here would tell the game the frame
    // decoded fine and leave it presenting stale tile memory.
    // `work` is unused by aurora's decoder, but pass it through so the signature keeps matching.
    const void* file = GuestPtr(fileAddr);
    const uint32_t result = static_cast<uint32_t>(
        THPVideoDecode(file, GuestPtr(tileYAddr), GuestPtr(tileUAddr), GuestPtr(tileVAddr), GuestPtr(workAddr)));

    // The guest decoder wrote its output through the locked cache, whose DMA the runtime reports
    // to the GX texture caches. A native decoder writes behind their back, so they kept serving the
    // first frame's planes wherever no other write happened to share a 64 KiB granule - movies
    // came out with live luma over the opening title card's neutral chroma: greyscale. Report
    // exactly what was written.
    u16 width = 0;
    u16 height = 0;
    if (THPVideoFrameSize(file, &width, &height)) {
        const uint32_t chromaWidth = (width + 1u) / 2u;
        const uint32_t chromaHeight = (height + 1u) / 2u;
        GxNotifyGuestRamDmaWrite(tileYAddr, TiledPlaneBytes(width, height));
        GxNotifyGuestRamDmaWrite(tileUAddr, TiledPlaneBytes(chromaWidth, chromaHeight));
        GxNotifyGuestRamDmaWrite(tileVAddr, TiledPlaneBytes(chromaWidth, chromaHeight));
    }
    return result;
}
PPC_NATIVE_OVERRIDE(801B3BAC, THP__VideoDecode_801b3bac, uint32_t,
                    (uint32_t fileAddr, uint32_t tileYAddr, uint32_t tileUAddr, uint32_t tileVAddr,
                     uint32_t workAddr),
                    (fileAddr, tileYAddr, tileUAddr, tileVAddr, workAddr));

// NOT overriding THPInit (0x801b6ee0). aurora's THPInit() is just `return TRUE`, because its
// decoder needs no setup - but the guest's is 39 real instructions, and the game's *own* THP code
// (THP::AudioDecoder and the player at 0x805508e8 onwards) still runs as translated PPC and may
// depend on whatever state that sets up. Replacing it with a stub left that code running against
// uninitialised state. Only the pure leaf decode function is safe to take over.
