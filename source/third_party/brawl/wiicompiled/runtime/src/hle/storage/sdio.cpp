// sdio.cpp - /dev/sdio/slot0, the Wii's SD card slot, over the image in sd_card.cpp.
//
// Host-controller ioctls and SD commands follow Dolphin's SDIOSlot0 (Source/Core/Core/IOS/SDIO/SDIOSlot0.cpp),
// the implementation Brawl mods such as Project+ are tested against. Brawl's own SD library predates SDHC, so a
// card of 2 GiB or less is a standard-capacity card (byte addressing, CSD v1); a bigger one is presented as SDHC.

#include "sdio.h"

#include "sd_card.h"

#include "memory.h"
#include "runtime_log.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void GxNotifyGuestRamDmaWrite(uint32_t addr, uint32_t size);
void NandQueueIosCallback(uint32_t callbackPtr, int32_t result, uint32_t callbackArg);

namespace {

enum : uint32_t {
    IOCTL_WRITEHCR = 1,
    IOCTL_READHCR = 2,
    IOCTL_RESETCARD = 4,
    IOCTL_SETCLK = 6,
    IOCTL_SENDCMD = 7,
    IOCTL_GETSTATUS = 11,
    IOCTL_GETOCR = 12,
};

enum : uint32_t {
    GO_IDLE_STATE = 0,
    ALL_SEND_CID = 2,
    SEND_RELATIVE_ADDR = 3,
    SELECT_CARD = 7,
    SEND_IF_COND = 8,
    SEND_CSD = 9,
    SEND_CID = 10,
    STOP_TRANSMISSION = 12,
    SEND_STATUS = 13,
    SET_BLOCKLEN = 16,
    READ_SINGLE_BLOCK = 17,
    READ_MULTIPLE_BLOCK = 18,
    WRITE_SINGLE_BLOCK = 24,
    WRITE_MULTIPLE_BLOCK = 25,
    APP_CMD_NEXT = 55,
    ACMD_SETBUSWIDTH = 6,
    ACMD_SENDOPCOND = 41,
    ACMD_SENDSCR = 51,
    EVENT_REGISTER = 0x40,
    EVENT_UNREGISTER = 0x41,
};

enum : uint32_t {
    CARD_NOT_EXIST = 0,
    CARD_INSERTED = 1,
    CARD_INITIALIZED = 0x10000,
    CARD_SDHC = 0x100000,
};

constexpr uint32_t HCR_CLOCKCONTROL = 0x2C;
constexpr uint32_t HCR_SOFTWARERESET = 0x2F;
constexpr int32_t RET_OK = 0;
constexpr int32_t RET_FAIL = 1;
constexpr int32_t IPC_EINVAL = -4;
constexpr int32_t EVENT_INVALID = 0xc210000;
constexpr uint64_t kStandardCardLimit = 2048ull * 1024 * 1024;

struct State {
    std::array<uint32_t, 0x200 / 4> registers{};
    uint32_t status = CARD_NOT_EXIST;
    uint32_t blockLength = 512;
    uint32_t busWidth = 0;
    bool eventPending = false;
    uint32_t eventCallback = 0;
    uint32_t eventCallbackArg = 0;
};

State g_state;

bool Sdhc() { return SdCard::SizeBytes() > kStandardCardLimit; }

// BRAWL_SD_TRACE=1 logs every slot request.
bool Trace() {
    static const bool on = std::getenv("BRAWL_SD_TRACE") != nullptr;
    return on;
}

// IOS hands the slot physical addresses (the SDK converts buffers before the IPC call); HLE callers pass virtual ones.
uint32_t Virtual(uint32_t address) { return address && address < 0x80000000u ? address | 0x80000000u : address; }

void Respond(uint32_t out, uint32_t outLen, uint32_t value) {
    if (out && outLen >= 4) Memory::TryWrite32(out, value);
}

uint32_t OcRegister() {
    uint32_t ocr = 0x00ff8000;
    if (g_state.status & CARD_INITIALIZED) ocr |= 0x80000000u;
    if (g_state.status & CARD_SDHC) ocr |= 0x40000000u;
    return ocr;
}

// The CSD as IOS returns it, Dolphin's GetCSDv1/GetCSDv2 word for word: the register without its CRC byte, shifted
// right by 8 bits, word 0 (the top) written last. Brawl's SD library and Project+'s SDHC code parse this layout.
std::array<uint32_t, 4> CardSpecificData() {
    const uint64_t bytes = SdCard::SizeBytes();
    if (g_state.status & CARD_SDHC) {
        const uint32_t cSize = static_cast<uint32_t>(bytes / (512 * 1024) - 1);
        return {0x00400e00, 0x5a5f5900, 0x0000007f | (cSize << 8), 0x800a4000};
    }
    // size = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2 + READ_BL_LEN)
    uint64_t size = bytes;
    uint32_t readBlLen = 9, cSizeMult = 0;
    while (size > 4096) {
        size >>= 1;
        if (++cSizeMult > 7 + 2 + readBlLen && ++readBlLen > 15) {
            size = 4096;
            cSizeMult = 7 + 2 + readBlLen;
        }
    }
    cSizeMult -= 2 + readBlLen;
    const uint32_t cSize = static_cast<uint32_t>(size - 1);
    return {0x000007f0, 0x035b5080 | (readBlLen << 8) | (cSize >> 10), 0x003ffc7f | (cSize << 22) | (cSizeMult << 7),
            0x80040040 | (readBlLen << 18)};
}

// A 16-byte register response (CSD, CID): word 0 at the end.
void RespondRegister(uint32_t out, uint32_t outLen, const std::array<uint32_t, 4>& words) {
    for (uint32_t i = 0; i < 4 && out && 4 * (4 - i) <= outLen; ++i) Memory::TryWrite32(out + 12 - 4 * i, words[i]);
}

struct Request {
    uint32_t command, type, response, arg, blocks, blockSize, address, isDma, pad;
};

bool ReadRequest(uint32_t in, Request& request) {
    uint32_t* fields = &request.command;
    for (int i = 0; i < 9; ++i) {
        if (!Memory::TryRead32(in + 4 * i, fields[i])) return false;
    }
    return true;
}

int32_t Transfer(const Request& request, bool write) {
    const uint64_t offset = (g_state.status & CARD_SDHC) ? uint64_t(request.arg) * 512 : request.arg;
    const uint32_t length = request.blockSize * request.blocks;
    const uint32_t buffer = Virtual(request.address);
    if (!length || !Memory::Contains(buffer, length)) return RET_FAIL;
    uint8_t* data = Memory::GetPointer(buffer, length);
    if (write) return SdCard::Write(offset, data, length) ? RET_OK : RET_FAIL;
    if (!SdCard::Read(offset, data, length)) {
        RT_LOGF(RT_TAG_NAND, "[sd] read of %u bytes at 0x%llX failed\n", length, static_cast<unsigned long long>(offset));
        return RET_FAIL;
    }
    GxNotifyGuestRamDmaWrite(buffer, length);
    return RET_OK;
}

int32_t ExecuteCommand(uint32_t in, uint32_t out, uint32_t outLen) {
    Request request{};
    if (!ReadRequest(in, request)) return IPC_EINVAL;
    if (out && outLen && Memory::Contains(out, outLen)) std::memset(Memory::GetPointer(out, outLen), 0, outLen);
    int32_t result = RET_OK;
    if (Trace()) {
        RT_LOGF(RT_TAG_NAND, "[sd] cmd %u arg 0x%08X blocks %u x %u addr 0x%08X\n", request.command, request.arg,
                request.blocks, request.blockSize, request.address);
    }
    switch (request.command) {
    case GO_IDLE_STATE:
        Respond(out, outLen, 0);
        break;
    case SEND_RELATIVE_ADDR:
        Respond(out, outLen, 0x9f62);
        break;
    case SEND_IF_COND:
        // Answered only by v2 cards; it also tells the host this one may be SDHC.
        if (Sdhc()) g_state.status |= CARD_SDHC;
        Respond(out, outLen, request.arg);
        break;
    case SEND_CSD:
        RespondRegister(out, outLen, CardSpecificData());
        break;
    case ALL_SEND_CID:
    case SEND_CID:
        RespondRegister(out, outLen, {0xAC68006B, 0x4E430403, 0x4C504849, 0x00D0444F});
        break;
    case SET_BLOCKLEN:
        g_state.blockLength = request.arg;
        Respond(out, outLen, 0x900);
        break;
    case APP_CMD_NEXT:
        Respond(out, outLen, 0x920);
        break;
    case ACMD_SETBUSWIDTH:
        g_state.busWidth = request.arg & 3;
        Respond(out, outLen, 0x920);
        break;
    case ACMD_SENDOPCOND:
        Respond(out, outLen, OcRegister());
        break;
    case ACMD_SENDSCR: {
        // SCR: SD spec 1.1 (2.0 for SDHC), 1- and 4-bit bus.
        const uint32_t buffer = Virtual(request.address);
        if (Memory::Contains(buffer, 8)) {
            Memory::TryWrite32(buffer, (g_state.status & CARD_SDHC) ? 0x02350000u : 0x01250000u);
            Memory::TryWrite32(buffer + 4, 0);
        }
        Respond(out, outLen, 0x920);
        break;
    }
    case SELECT_CARD:
        Respond(out, outLen, (request.arg >> 16) ? 0x700 : 0x900);
        break;
    case STOP_TRANSMISSION:
        Respond(out, outLen, 0xb00);
        break;
    case SEND_STATUS:
        Respond(out, outLen, 0x900);
        break;
    case READ_SINGLE_BLOCK:
    case READ_MULTIPLE_BLOCK:
        result = Transfer(request, false);
        Respond(out, outLen, 0x900);
        break;
    case WRITE_SINGLE_BLOCK:
    case WRITE_MULTIPLE_BLOCK:
        result = Transfer(request, true);
        Respond(out, outLen, 0x900);
        break;
    case EVENT_REGISTER:
        // IOS answers only when the card is inserted or removed, which never happens here.
        g_state.eventPending = true;
        return Sdio::kNoReply;
    case EVENT_UNREGISTER:
        if (!g_state.eventPending) return IPC_EINVAL;
        g_state.eventPending = false;
        if (g_state.eventCallback) NandQueueIosCallback(g_state.eventCallback, EVENT_INVALID, g_state.eventCallbackArg);
        g_state.eventCallback = 0;
        break;
    default:
        RT_LOGF(RT_TAG_NAND, "[sd] unhandled SD command %u (arg 0x%08X)\n", request.command, request.arg);
        break;
    }
    return result;
}

} // namespace

namespace Sdio {

int32_t Ioctl(uint32_t cmd, uint32_t in, uint32_t inLen, uint32_t out, uint32_t outLen) {
    (void)inLen;
    in = Virtual(in);
    out = Virtual(out);
    if (Trace() && cmd != IOCTL_SENDCMD) RT_LOGF(RT_TAG_NAND, "[sd] ioctl %u\n", cmd);
    switch (cmd) {
    case IOCTL_WRITEHCR: {
        uint32_t reg = 0, value = 0;
        if (!Memory::TryRead32(in, reg) || !Memory::TryRead32(in + 16, value) || reg >= g_state.registers.size()) return IPC_EINVAL;
        if (reg == HCR_CLOCKCONTROL && (value & 1)) value |= 2;  // internal clock enabled: report it stable
        else if (reg == HCR_SOFTWARERESET && value) value = 0;    // a reset clears the register
        g_state.registers[reg] = value;
        return RET_OK;
    }
    case IOCTL_READHCR: {
        uint32_t reg = 0;
        if (!Memory::TryRead32(in, reg) || reg >= g_state.registers.size()) return IPC_EINVAL;
        Respond(out, outLen, g_state.registers[reg]);
        return RET_OK;
    }
    case IOCTL_RESETCARD:
        if (SdCard::Present()) g_state.status |= CARD_INITIALIZED;
        Respond(out, outLen, 0x9f620000);  // RCA in the upper half, 0 = success
        return RET_OK;
    case IOCTL_SETCLK:
        return RET_OK;
    case IOCTL_SENDCMD:
        return ExecuteCommand(in, out, outLen);
    case IOCTL_GETSTATUS:
        // As Dolphin: IOS initializes the card itself, and reports an SDHC card as one (Project+'s SDHC code
        // switches to block addressing on that bit).
        if (SdCard::Present()) g_state.status |= CARD_INSERTED | CARD_INITIALIZED | (Sdhc() ? CARD_SDHC : 0);
        else g_state.status = CARD_NOT_EXIST;
        Respond(out, outLen, g_state.status);
        return RET_OK;
    case IOCTL_GETOCR:
        Respond(out, outLen, OcRegister());
        return RET_OK;
    default:
        RT_LOGF(RT_TAG_NAND, "[sd] unhandled /dev/sdio/slot0 ioctl %u\n", cmd);
        return RET_OK;
    }
}

int32_t Ioctlv(uint32_t cmd, uint32_t numIn, uint32_t numOut, uint32_t vectorPtr) {
    if (cmd != IOCTL_SENDCMD || numIn < 1 || numOut < 1 || !Memory::Contains(vectorPtr, (numIn + numOut) * 8)) {
        RT_LOGF(RT_TAG_NAND, "[sd] unhandled /dev/sdio/slot0 ioctlv %u (%u in, %u out)\n", cmd, numIn, numOut);
        return IPC_EINVAL;
    }
    const uint32_t in = Virtual(Memory::Read32(vectorPtr));
    const uint32_t out = Virtual(Memory::Read32(vectorPtr + numIn * 8));
    const uint32_t outLen = Memory::Read32(vectorPtr + numIn * 8 + 4);
    return ExecuteCommand(in, out, outLen);
}

void SetPendingEvent(uint32_t callback, uint32_t callbackArg) {
    g_state.eventCallback = callback;
    g_state.eventCallbackArg = callbackArg;
}

} // namespace Sdio
