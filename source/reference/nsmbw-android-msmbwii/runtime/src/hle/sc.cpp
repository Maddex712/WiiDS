#include "hle_stubs.h"

#include "console_identity.h"
#include "sc_serial_contract.h"
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "memory.h"
#include "runtime_config.h"
#include "aurora_events.h"
#include "runtime_log.h"

namespace {

// Use the SDK's own value tables, including its unknown-region result.
uint32_t LookupProductRegion(uint32_t table, uint32_t stride, uint32_t count,
                             const std::string& value) {
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t entry = table + index * stride;
        if (!Memory::Contains(entry, stride)) {
            break;
        }
        const auto* bytes = static_cast<const uint8_t*>(Memory::GetPointer(entry, stride));
        if (bytes[0] == 0xFF) {
            break;
        }
        if (value.size() < stride - 1 &&
            std::memcmp(bytes + 1, value.c_str(), value.size() + 1) == 0) {
            return bytes[0];
        }
    }
    return 0xFFFFFFFFu;
}

} // namespace

// SCCheckStatus is polled in OSInit's busy loop (while(SCCheckStatus()==1) waits on async SYSCONF
// load via NAND IPC); we have no async IPC callbacks, so return 0 (SUCCESS) immediately.

// 0x801DB8E0 -> SCCheckStatus()
// Returns: 0 = success, 1 = busy, 2 = error
extern "C" uint32_t SCCheckStatus_HLE()
{
    // Return 0 (success) immediately to avoid infinite busy-wait in OSInit
    return 0;
}

PPC_NATIVE_OVERRIDE(801DB8E0, SCCheckStatus_HLE, uint32_t, (), ());

// 0x801DD1D0 -> SCGetAspectRatio()
// Returns: 0 = 4:3, 1 = 16:9. The display settings' aspect at boot: 16:9 and Fill both run the
// game in widescreen (Fill then widens its canvas further).
extern "C" uint32_t SCGetAspectRatio_HLE()
{
    return DisplaySettings::GameWidescreen() ? 1u : 0u;
}

PPC_NATIVE_OVERRIDE(801DD1D0, SCGetAspectRatio_HLE, uint32_t, (), ());

// 0x801DD2B0 -> SCGetEuRgb60Mode()
// Returns: 0 = PAL50, 1 = PAL60/RGB60
extern "C" uint32_t SCGetEuRgb60Mode_HLE()
{
    // Default to PAL60 so PAL builds do not fall back to the half-rate PAL50
    // sync path on modern displays. Actual texture/cache correctness is handled
    // elsewhere; this only exposes the intended SYSCONF setting.
    return 1;
}

PPC_NATIVE_OVERRIDE(801DD2B0, SCGetEuRgb60Mode_HLE, uint32_t, (), ());

// Expose the selected emulated NAND identity through the SDK SC APIs.

extern "C" uint32_t SCGetProductArea_HLE()
{
    return LookupProductRegion(0x80343A08u, 5, 13,  // SC area table {code, "JPN"...} (NSMBW)
                               RuntimeConsoleIdentity::Current().area);
}

PPC_NATIVE_OVERRIDE(801DD860, SCGetProductArea_HLE, uint32_t, (), ());

#if 0  // NSMBW: SCGetProductCode is not linked into NSMBW; this wrote MKW's SC CODE buffer
extern "C" uint32_t SCGetProductCode_HLE()
{
    // Original PAL SC storage for the six-byte CODE value.
    constexpr uint32_t kProductCodeAddress = 0x803869E0u;
    const std::string& productCode = RuntimeConsoleIdentity::Current().productCode;
    const size_t size = productCode.size() + 1;
    if (!Memory::Contains(kProductCodeAddress, size)) {
        return 0;
    }
    std::memcpy(Memory::GetPointer(kProductCodeAddress, size),
                productCode.c_str(), size);
    return kProductCodeAddress;
}
#endif

#if 0  // NSMBW: SCGetProductCode is not linked into NSMBW (was MKW-only)
PPC_NATIVE_OVERRIDE(801B2424, SCGetProductCode_HLE, uint32_t, (), ());
#endif

extern "C" uint32_t SCGetProductSN_HLE(uint32_t serialAddress)
{
    const std::string& serial = RuntimeConsoleIdentity::Current().serial;
    return RuntimeScSerial::Write(serial, serialAddress,
        [](uint32_t address, size_t size) { return Memory::Contains(address, size); },
        [](uint32_t address, uint32_t value) { Memory::Write32(address, value); });
}

#if 0  // NSMBW: SCGetProductSN is not linked into NSMBW (was MKW-only)
PPC_NATIVE_OVERRIDE(801B2460, SCGetProductSN_HLE, uint32_t, (uint32_t serialAddress), (serialAddress));
#endif

extern "C" uint32_t SCGetProductGameRegion_HLE()
{
    return LookupProductRegion(0x80343A50u, 4, 4,  // SC game-region table {code, "JP"...} (NSMBW)
                               RuntimeConsoleIdentity::Current().gameRegion);
}

PPC_NATIVE_OVERRIDE(801DD940, SCGetProductGameRegion_HLE, uint32_t, (), ());

// These stubs make the game think all titles are installed; otherwise it checks title ID
// 0x00010004524d4350 ("RMCP", Mario Kart Wii PAL) and reports error code 5.

// 0x801AE4A0 -> OS__IsTitleInstalled(titleIdHi, titleIdLo)
// Returns: 1 = installed, 0 = not installed
extern "C" uint32_t OS__IsTitleInstalled(uint32_t titleIdHi, uint32_t titleIdLo)
{
    RT_LOGF(RT_TAG_HLE, "CINS: OSIsTitleInstalled(0x%08X%08X) -> 1 (stubbed as installed)\n",
            titleIdHi, titleIdLo);
    return 1; // Always report installed
}

#if 0  // NSMBW: OSIsTitleInstalled is not linked into NSMBW (was MKW-only)
PPC_NATIVE_OVERRIDE(801AE4A0, OS__IsTitleInstalled, uint32_t, (uint32_t titleIdHi, uint32_t titleIdLo), (titleIdHi, titleIdLo));
#endif

// 0x801AD1D4 -> OS__CheckInstall(requiredBlocks, titleIdHi, titleIdLo, outFlagsPtr): returns 0 with
// outFlagsPtr = 0x3 (bit0 has data, bit1 has update; bit2 would be needs-blocks) i.e. fully installed.
extern "C" uint32_t OS__CheckInstall(uint32_t requiredBlocks, uint32_t titleIdHi, 
                                      uint32_t titleIdLo, uint32_t outFlagsPtr)
{
    RT_LOGF(RT_TAG_HLE, "OS__CheckInstall(blocks=%u, 0x%08X%08X) -> success (stubbed)\n",
            requiredBlocks, titleIdHi, titleIdLo);
    if (outFlagsPtr != 0) {
        Memory::Write32(outFlagsPtr, 0x3); // has data + has update = fully installed
    }
    return 0; // Success
}

#if 0  // NSMBW: OSCheckInstall is not linked into NSMBW (was MKW-only)
PPC_NATIVE_OVERRIDE(801AD1D4, OS__CheckInstall, uint32_t, (uint32_t requiredBlocks, uint32_t titleIdHi, uint32_t titleIdLo, uint32_t outFlagsPtr), (requiredBlocks, titleIdHi, titleIdLo, outFlagsPtr));
#endif
