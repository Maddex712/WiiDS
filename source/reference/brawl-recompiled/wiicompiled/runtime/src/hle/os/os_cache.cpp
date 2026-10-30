// Data/instruction/locked cache maintenance HLE.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <aurora/aurora.h>

#include "abi_bridge.h"
#include "memory.h"
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "runtime_log.h"
#include "hle/gx/gx_internal.h"

// ============================================================================
// Data cache maintenance (DCInvalidate/Flush/Store)
// These touch hardware on console; on host we fast-path them to avoid huge
// translated loops while still validating the guest range.
// ============================================================================
namespace {
struct CacheRange {
    uint32_t start = 0;
    uint32_t size = 0;
};

constexpr uint32_t kCacheLineSize = 32;

bool NormalizeCacheRange(uint32_t addr, uint32_t length, CacheRange& out)
{
    if (length == 0) {
        return false;
    }

    const uint32_t alignedStart = addr & ~(kCacheLineSize - 1u);
    const uint64_t end = static_cast<uint64_t>(addr) + static_cast<uint64_t>(length) + (kCacheLineSize - 1u);
    const uint64_t alignedEnd = end & ~(static_cast<uint64_t>(kCacheLineSize) - 1u);
    if (alignedEnd <= alignedStart || alignedEnd >= 0x100000000ull) {
        return false;
    }

    out.start = alignedStart;
    out.size = static_cast<uint32_t>(alignedEnd - alignedStart);
    return out.size != 0;
}

bool ValidateCacheRange(const char* label, const CacheRange& range)
{
    if (range.size == 0) {
        return false;
    }

    if (!::Memory::Contains(range.start, range.size)) {
        RT_LOG(RT_TAG_OS) << label << ": range 0x" << std::hex << range.start
                  << " len=0x" << range.size << std::dec
                  << " outside guest memory; skipping" << std::endl;
        return false;
    }
    return true;
}

void DcRangeOp(const char* label, uint32_t addr, uint32_t length)
{
    CacheRange range{};
    if (!NormalizeCacheRange(addr, length, range) || !ValidateCacheRange(label, range)) {
        return;
    }

    DownloadEfbCopiesForRange(range.start, range.size);
    GxNotifyGuestRamDmaWrite(range.start, range.size);
}
} // namespace

extern "C" void DCInvalidateRange_801a1600(uint32_t addr, uint32_t length)
{
    DcRangeOp("DCInvalidateRange_801a1600", addr, length);
}

extern "C" void DCFlushRange_801a162c(uint32_t addr, uint32_t length)
{
    DcRangeOp("DCFlushRange_801a162c", addr, length);
}

// Brawl's screen picture (fn_8002F4B4, the Classic score screen and the like) converts the XFB on screen, 640x480
// YUYV, into a 640x480 RGBA8 texture with the CPU and then stores it with DCStoreRange. The XFB here only ever
// existed on the GPU, so its RAM holds nothing (the picture came out solid green, issue #1): put the frame on
// screen into the texture before the store instead.
void FillScreenPicture(uint32_t addr, uint32_t length)
{
    constexpr uint32_t kWidth = 640, kHeight = 480;
    const CpuContext* ctx = TryGetCpuContext();
    if (length != kWidth * kHeight * 4) {
        return;
    }
    if (!Memory::Contains(addr, length)) {
        return;
    }
    // The return address names the caller when the translated call set it; otherwise the picture is recognised by
    // what an XFB that was never written converts to: every texel the same green (Y, U and V all 0).
    const uint8_t* tile = Memory::GetPointer(addr, 64);
    bool green = true;
    for (int i = 0; i < 16 && green; ++i) {
        green = tile[i * 2] == 255 && tile[i * 2 + 1] < 16 && tile[32 + i * 2] > 100 && tile[32 + i * 2 + 1] < 16 &&
                std::memcmp(tile + i * 2, tile, 2) == 0 && std::memcmp(tile + 32 + i * 2, tile + 32, 2) == 0;
    }
    RT_LOGF(RT_TAG_GX, "screen picture: store of 0x%08X, lr 0x%08X, green %d\n", addr, ctx != nullptr ? ctx->lr : 0u,
            green ? 1 : 0);
    if (!green && (ctx == nullptr || ctx->lr != 0x8002F554u)) {
        return;
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(kWidth) * kHeight * 4);
    if (!aurora_read_display_copy(rgba.data(), kWidth, kHeight)) {
        RT_LOGF(RT_TAG_GX, "screen picture: the frame on screen could not be read back\n");
        return;
    }
    // GX RGBA8: 4x4 tiles in rows, each 32 bytes of A,R then 32 bytes of G,B for its 16 texels.
    uint8_t* out = Memory::GetPointer(addr, length);
    for (uint32_t ty = 0; ty < kHeight / 4; ++ty) {
        for (uint32_t tx = 0; tx < kWidth / 4; ++tx, out += 64) {
            for (uint32_t i = 0; i < 16; ++i) {
                const uint8_t* p = &rgba[((ty * 4 + i / 4) * kWidth + tx * 4 + i % 4) * 4];
                out[i * 2] = p[3];
                out[i * 2 + 1] = p[0];
                out[32 + i * 2] = p[1];
                out[32 + i * 2 + 1] = p[2];
            }
        }
    }
}

extern "C" void DCStoreRange_801a165c(uint32_t addr, uint32_t length)
{
    FillScreenPicture(addr, length);
    DcRangeOp("DCStoreRange_801a165c", addr, length);
}

extern "C" void DCFlushRangeNoSync_801a168c(uint32_t addr, uint32_t length)
{
    DcRangeOp("DCFlushRangeNoSync_801a168c", addr, length);
}

extern "C" void DCStoreRangeNoSync_801a16b8(uint32_t addr, uint32_t length)
{
    DcRangeOp("DCStoreRangeNoSync_801a16b8", addr, length);
}

PPC_NATIVE_OVERRIDE_VOID(801D76BC, DCInvalidateRange_801a1600, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(801D76E8, DCFlushRange_801a162c, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(801D7718, DCStoreRange_801a165c, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(801D7748, DCFlushRangeNoSync_801a168c, (uint32_t addr, uint32_t length), (addr, length));
#if 0  // SPM: DCStoreRangeNoSync: name not in SPM
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE_VOID(801AC500, DCStoreRangeNoSync_801a16b8, (uint32_t addr, uint32_t length), (addr, length));
#endif
#endif

// ----------------------------------------------------------------------------
// CPU Cache Maintenance Stubs (DC/IC/LC)
// These are safe to no-op because the host CPU handles caching.
// ----------------------------------------------------------------------------
extern "C" void Cache_Maintenance_Stub()
{
}

namespace {

constexpr uint32_t kCacheOpLineSize = 32u;
constexpr uint32_t kLcOpPageSize = 4096u;
constexpr uint32_t kLcOpMaxBlocksPerTransfer = 128u;

uint32_t AlignDown32(uint32_t value)
{
    return value & ~(kCacheOpLineSize - 1u);
}

uint32_t AlignUp32(uint32_t value)
{
    return (value + (kCacheOpLineSize - 1u)) & ~(kCacheOpLineSize - 1u);
}

uint32_t DecodeLcBlockCount(uint32_t encodedBlockCount)
{
    const uint32_t blocks = encodedBlockCount & 0x7Fu;
    return blocks == 0 ? kLcOpMaxBlocksPerTransfer : blocks;
}

bool CopyGuestRange(uint32_t dstAddr, uint32_t srcAddr, uint32_t len, const char* opName)
{
    if (len == 0) {
        return true;
    }

    try {
        auto* dst = ::Memory::GetPointer(dstAddr, len);
        auto* src = ::Memory::GetPointer(srcAddr, len);
        std::memmove(dst, src, len);
        return true;
    } catch (const ::Memory::AccessViolation& e) {
        RT_LOG(RT_TAG_OS) << opName << ": memory access failed @0x" << std::hex << e.address()
                  << " len=0x" << len << std::dec << " (" << e.reason() << ")" << std::endl;
        return false;
    }
}

} // namespace

extern "C" void DCZeroRange_HLE_801a16e4(CpuContext* ctx)
{
    const uint32_t addr = static_cast<uint32_t>(ctx->gpr[3]);
    const uint32_t len = static_cast<uint32_t>(ctx->gpr[4]);
    if (len == 0) {
        return;
    }

    const uint32_t alignedAddr = AlignDown32(addr);
    const uint32_t alignedLen = AlignUp32((addr - alignedAddr) + len);
    try {
        auto* dst = ::Memory::GetPointer(alignedAddr, alignedLen);
        std::memset(dst, 0, alignedLen);
        GxNotifyGuestRamDmaWrite(alignedAddr, alignedLen);
    } catch (const ::Memory::AccessViolation& e) {
        RT_LOG(RT_TAG_OS) << "DCZeroRange: memory access failed @0x" << std::hex << e.address()
                  << " len=0x" << alignedLen << std::dec << " (" << e.reason() << ")" << std::endl;
    }
}

extern "C" void LCLoadBlocks_HLE_801a1894(CpuContext* ctx)
{
    const uint32_t dstAddr = static_cast<uint32_t>(ctx->gpr[3]);
    const uint32_t srcAddr = static_cast<uint32_t>(ctx->gpr[4]);
    const uint32_t blocks = DecodeLcBlockCount(static_cast<uint32_t>(ctx->gpr[5]));
    const uint32_t len = blocks * kCacheOpLineSize;
    if (CopyGuestRange(dstAddr, srcAddr, len, "LCLoadBlocks")) {
        // RAM->LC loads are DMA reads on console, but the destination range is
        // still a guest RAM alias as far as the runtime is concerned; notify it
        // the same way LCStoreBlocks below does.
        GxNotifyGuestRamDmaWrite(dstAddr, len);
    }
}

extern "C" void LCStoreBlocks_HLE_801a18b8(CpuContext* ctx)
{
    const uint32_t dstAddr = static_cast<uint32_t>(ctx->gpr[3]);
    const uint32_t srcAddr = static_cast<uint32_t>(ctx->gpr[4]);
    const uint32_t blocks = DecodeLcBlockCount(static_cast<uint32_t>(ctx->gpr[5]));
    const uint32_t len = blocks * kCacheOpLineSize;
    if (CopyGuestRange(dstAddr, srcAddr, len, "LCStoreBlocks")) {
        // LC->RAM stores are DMA writes on console; no flush follows them.
        GxNotifyGuestRamDmaWrite(dstAddr, len);
    }
}

extern "C" uint32_t LCStoreData_HLE_801a18dc(CpuContext* ctx)
{
    const uint32_t dstAddr = static_cast<uint32_t>(ctx->gpr[3]);
    const uint32_t srcAddr = static_cast<uint32_t>(ctx->gpr[4]);
    const uint32_t len = static_cast<uint32_t>(ctx->gpr[5]);
    if (CopyGuestRange(dstAddr, srcAddr, len, "LCStoreData") && len != 0) {
        GxNotifyGuestRamDmaWrite(dstAddr, len);
    }

    const uint32_t pagesQueued = len == 0 ? 0u : ((len + kLcOpPageSize - 1u) / kLcOpPageSize);
    ctx->gpr[3] = pagesQueued;
    return pagesQueued;
}

extern "C" uint32_t LCQueueLength_HLE_801a197c(CpuContext*)
{
    // We execute LC transfers synchronously, so the DMA queue is always drained.
    return 0;
}

extern "C" void LCQueueWait_HLE_801a1988(CpuContext*)
{
    // Synchronous HLE copy completes immediately.
}

PPC_NATIVE_OVERRIDE_VOID(801d76a8, Cache_Maintenance_Stub, (), ()); // DCEnable
PPC_NATIVE_OVERRIDE_VOID(801d77a0, DCZeroRange_HLE_801a16e4, (CpuContext* ctx), (ctx)); // DCZeroRange
PPC_NATIVE_OVERRIDE_VOID(801d77cc, Cache_Maintenance_Stub, (), ()); // ICInvalidateRange
PPC_NATIVE_OVERRIDE_VOID(801d7800, Cache_Maintenance_Stub, (), ()); // ICFlashInvalidate
PPC_NATIVE_OVERRIDE_VOID(801d7810, Cache_Maintenance_Stub, (), ()); // ICEnable
#if 0  // SPM: __LCEnable: name not in SPM
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE_VOID(801ac5d0, Cache_Maintenance_Stub, (), ()); // __LCEnable
#endif
#endif
#if 0  // SPM: LCEnable: name not in SPM
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE_VOID(801ac6a0, Cache_Maintenance_Stub, (), ()); // LCEnable
#endif
#endif
PPC_NATIVE_OVERRIDE_VOID(801d7928, Cache_Maintenance_Stub, (), ()); // LCDisable
#if 0  // SPM: LCLoadBlocks: name not in SPM
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE_VOID(801ac710, LCLoadBlocks_HLE_801a1894, (CpuContext* ctx), (ctx)); // LCLoadBlocks
#endif
#endif
#if 0  // SPM: LCStoreBlocks: name not in SPM
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE_VOID(801ac740, LCStoreBlocks_HLE_801a18b8, (CpuContext* ctx), (ctx)); // LCStoreBlocks
#endif
#endif
#if 0  // SPM: LCStoreData: name not in SPM
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE(801ac770, LCStoreData_HLE_801a18dc, uint32_t, (CpuContext* ctx), (ctx)); // LCStoreData
#endif
#endif
#if 0  // SPM: LCQueueLength: name not in SPM
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE(801ac810, LCQueueLength_HLE_801a197c, uint32_t, (CpuContext* ctx), (ctx)); // LCQueueLength
#endif
#endif
#if 0  // SPM: LCQueueWait: name not in SPM
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE_VOID(801ac820, LCQueueWait_HLE_801a1988, (CpuContext* ctx), (ctx)); // LCQueueWait
#endif
#endif
PPC_NATIVE_OVERRIDE_VOID(801d7c40, Cache_Maintenance_Stub, (), ()); // OS____CacheInit
