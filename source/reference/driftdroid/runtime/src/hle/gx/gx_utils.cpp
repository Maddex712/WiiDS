#include "gx_internal.h"
#include "runtime_log.h"

extern "C" {
int g_gxFrameCount = 0;

int32_t g_scissorLeft = 0;
int32_t g_scissorTop = 0;
int32_t g_scissorWidth = 0;
int32_t g_scissorHeight = 0;

float g_viewportState[6] = {0.f, 0.f, 1.f, 1.f, 0.f, 1.f};
float g_projectionVector[7] = {1.f, 1.f, 0.f, 1.f, 0.f, -1.f, 0.f};
}

bool g_alphaCompareValid = false;

TexCopyState g_texCopyState;

GXColor DecodeGxColor(uint32_t colorWord) {
    GXColor color{};
    color.r = static_cast<uint8_t>((colorWord >> 24) & 0xFF);
    color.g = static_cast<uint8_t>((colorWord >> 16) & 0xFF);
    color.b = static_cast<uint8_t>((colorWord >> 8) & 0xFF);
    color.a = static_cast<uint8_t>(colorWord & 0xFF);
    return color;
}

void WriteGuestFloat(uint32_t addr, float value, const char* label) {
    if (addr == 0) return;
    try { Memory::WriteFloat32(addr, value); } catch (const Memory::AccessViolation& e) { LogMemoryError(RT_TAG_GX, label ? label : "GX write", e); }
}

void* GuestToHostPtr(uint32_t addr, size_t len) {
    if (addr == 0) return nullptr;
    // Hot path for the GX HLE (~1.8% of the game thread in profiles): the page-table lookup is
    // header-inline, so a hit avoids both the out-of-line Memory::GetPointer call and the
    // exception frame the fallback needs.
    if (uint8_t* fast = MemoryInline::GetPointerFast(addr, len == 0 ? 1 : len)) {
        return fast;
    }
    try { return Memory::GetPointer(addr, len); } catch (const Memory::AccessViolation& e) { LogMemoryError(RT_TAG_GX, "GX guest pointer", e); return nullptr; }
}

void WriteGuest32(uint32_t addr, uint32_t value, const char* label) {
    if (addr == 0) return;
    try { Memory::Write32(addr, value); } catch (const Memory::AccessViolation& e) { LogMemoryError(RT_TAG_GX, label ? label : "GX write32", e); }
}

bool IsKnownTexFormat(uint32_t fmt) {
    switch (fmt) {
    case GX_TF_I4: case GX_TF_I8: case GX_TF_IA4: case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3: case GX_TF_RGBA8: case GX_TF_CMPR: case GX_TF_C4: case GX_TF_C8: case GX_TF_C14X2: case GX_TF_Z8: case GX_TF_Z16: case GX_TF_Z24X8: case GX_CTF_R4: case GX_CTF_RA4: case GX_CTF_RA8: case GX_CTF_YUVA8: case GX_CTF_A8: case GX_CTF_R8: case GX_CTF_G8: case GX_CTF_B8: case GX_CTF_RG8: case GX_CTF_GB8: case GX_CTF_Z4: case GX_CTF_Z8M: case GX_CTF_Z8L: case GX_CTF_Z16L: return true;
    default: return false;
    }
}

bool IsPaletteTexFormat(uint32_t fmt) { return fmt == GX_TF_C4 || fmt == GX_TF_C8 || fmt == GX_TF_C14X2; }
bool IsKnownTlutFormat(uint32_t fmt) { switch (fmt) { case GX_TL_IA8: case GX_TL_RGB565: case GX_TL_RGB5A3: return true; default: return false; } }

// Returns false when the guest's TLUT descriptor is unusable. Callers must not
// hand the descriptor to aurora in that case: aurora reads entries*2 bytes from
// the pointer at load time, so a bad address or entry count is an out-of-range
// native read. This used to only log and let the call through, which is why the
// return value now exists.
bool ValidateTlutData(uint32_t objAddr, const TlutObjMeta& meta) {
    if (meta.dataAddr == 0 || meta.entries == 0) { RT_LOGF(RT_TAG_GX, "invalid TLUT data (oa=0x%08X)\n", objAddr); return false; }
    if (!IsKnownTlutFormat(meta.format)) { RT_LOGF(RT_TAG_GX, "invalid TLUT format 0x%X (oa=0x%08X)\n", meta.format, objAddr); return false; }
    if (!Memory::Contains(meta.dataAddr, static_cast<uint32_t>(meta.entries) * 2u)) {
        RT_LOGF(RT_TAG_GX, "invalid TLUT range (oa=0x%08X data=0x%08X entries=%u)\n", objAddr, meta.dataAddr, meta.entries);
        return false;
    }
    return true;
}
