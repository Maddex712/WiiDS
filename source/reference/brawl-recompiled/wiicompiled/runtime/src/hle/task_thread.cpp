#include "hle_stubs.h"
#include "abi_bridge.h"
#include "ppc_runtime.h"
#include "memory.h"
#include "runtime_log.h"

#if 0  // NSMBW: MKW-only. NSMBW links no EGG::TaskThread and no THP player (no .thp files on its disc).
namespace {

constexpr uint32_t kTaskMessageQueueOffset = 0x0Cu;
constexpr uint32_t kTaskCurrentJobOffset = 0x48u;
constexpr uint32_t kTaskDoneQueueOffset = 0x54u;

constexpr uint32_t kJobCallbackOffset = 0x00u;
constexpr uint32_t kJobArgOffset = 0x04u;
constexpr uint32_t kJobTokenOffset = 0x08u;
constexpr uint32_t kJobUnknown3Offset = 0x0Cu;
constexpr uint32_t kJobUnknown4Offset = 0x10u;
constexpr uint32_t kJobOnDoneOffset = 0x14u;
constexpr uint32_t kJobSize = 0x18u;

bool ShouldRetryThpPrepareAfterClose(uint32_t callback, uint32_t arg, uint32_t result)
{
    if (callback != 0x80529D68u || result != 0) {
        return false;
    }
    if (arg == 0 || !Memory::Contains(arg + 0xACu, 4)) {
        return false;
    }
    const uint32_t state = Memory::Read32(arg + 0xACu);
    if (state != 0) {
        return false;
    }
    if (!Memory::Contains(0x809BECF0u, 4) || !Memory::Contains(0x809BEBA0u, 4)) {
        return false;
    }
    const uint32_t thpInit = Memory::Read32(0x809BECF0u);
    const uint32_t thpOpen = Memory::Read32(0x809BEBA0u);
    return thpInit != 0 && thpOpen != 0;
}

uint32_t RunMovieManagerPrepareAsync(uint32_t manager, CpuContext* cpu)
{
    if (!cpu || manager == 0 || !Memory::Contains(manager + 0xACu, 4)) {
        return 0;
    }

    if (Memory::Read32(manager + 0xACu) != 0) {
        return cpu->gpr[3];
    }

    auto callThpPlayerOpen = [&]() -> uint32_t {
        CpuContextScope scope(cpu);
        cpu->gpr[3] = manager + 0x28u;
        cpu->gpr[4] = 0;
        InvokeIndirectCpu(0x80550CC0u, cpu);
        return cpu->gpr[3];
    };

    uint32_t openResult = callThpPlayerOpen();
    if (openResult == 0 && ShouldRetryThpPrepareAfterClose(0x80529D68u, manager, 0)) {
        {
            CpuContextScope scope(cpu);
            InvokeIndirectCpu(0x80551658u, cpu); // THP__PlayerStop
            InvokeIndirectCpu(0x80550F48u, cpu); // THP__PlayerClose
        }
        if (Memory::Contains(0x809BEBA0u, 8) && Memory::Read32(0x809BEBA0u) != 0) {
            Memory::Write32(0x809BEBA0u, 0);
            Memory::Write8(0x809BEBA4u, 0);
            Memory::Write8(0x809BEBA5u, 0);
            Memory::Write8(0x809BEBA6u, 0);
            Memory::Write8(0x809BEBA7u, 0);
        }
        openResult = callThpPlayerOpen();
    }

    if (openResult == 0) {
        cpu->gpr[3] = 0;
        return 0;
    }

    {
        CpuContextScope scope(cpu);
        cpu->gpr[3] = manager + 0x10u;
        InvokeIndirectCpu(0x80551D38u, cpu); // THP__PlayerGetVideoInfo
        InvokeIndirectCpu(0x80550F9Cu, cpu); // THP__PlayerCalcNeedMemory
    }
    const uint32_t needMemory = cpu->gpr[3];
    Memory::Write32(manager + 0x20u, needMemory);
    Memory::Write32(manager + 0x24u, 0);

    const uint32_t setBufferArg = Memory::Read32(manager + 0x1Cu);
    {
        CpuContextScope scope(cpu);
        cpu->gpr[3] = setBufferArg;
        InvokeIndirectCpu(0x80551054u, cpu); // THP__PlayerSetBuffer
    }

    const uint32_t mode = Memory::Read32(manager + 0xA8u);
    const uint32_t audioSystem = Memory::Read32(0x8088FDB8u + (mode << 2));
    uint32_t prepareResult = 0;
    {
        CpuContextScope scope(cpu);
        cpu->gpr[3] = Memory::Read32(manager + 0x24u);
        cpu->gpr[4] = audioSystem;
        cpu->gpr[5] = 0;
        InvokeIndirectCpu(0x80551378u, cpu); // THP__PlayerPrepare
        prepareResult = cpu->gpr[3];
    }
    if (prepareResult != 0) {
        Memory::Write32(manager + 0xACu, 1);
    }
    cpu->gpr[3] = prepareResult;
    return prepareResult;
}

void ClearTaskJob(uint32_t taskThread, uint32_t job)
{
    if (job == 0 || !Memory::Contains(job, kJobSize)) {
        Memory::Write32(taskThread + kTaskCurrentJobOffset, 0);
        return;
    }

    Memory::Write32(job + kJobCallbackOffset, 0);
    Memory::Write32(taskThread + kTaskCurrentJobOffset, 0);
    Memory::Write32(job + kJobCallbackOffset, 0);
    Memory::Write32(job + kJobUnknown3Offset, 0);
    Memory::Write32(job + kJobUnknown4Offset, 0);
    Memory::Write32(job + kJobOnDoneOffset, 0);
}

} // namespace

// HLE override for TaskThread_run (0x80242D7C).
// This mirrors the worker loop from the original implementation so job slots
// are retired exactly when the callback has finished.
extern "C" void TaskThread_run_HLE_80242d7c(CpuContext* ctx) {
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    const uint32_t taskThread = cpu->gpr[3];
    if (!Memory::Contains(taskThread + kTaskMessageQueueOffset, 0x4Cu)) {
        return;
    }

    // Match the original PPC prologue: TaskThread::Run seeds GQR2-GQR5 before
    // servicing callbacks. Some worker callbacks rely on these quantization
    // registers being initialized on the task thread.
    cpu->gqr[2] = 0x00040004u;
    cpu->gqr[3] = 0x00050005u;
    cpu->gqr[4] = 0x00060006u;
    cpu->gqr[5] = 0x00070007u;

    while (true) {
        const uint32_t stackPointer = cpu->gpr[1];
        const uint32_t outMsgPtr = stackPointer >= 0x20u ? (stackPointer - 0x20u) : 0;
        if (outMsgPtr == 0 || !Memory::Contains(outMsgPtr, 4)) {
            RT_LOG(RT_TAG_HLE) << "TaskThread_run: invalid guest stack scratch for task thread 0x"
                      << std::hex << taskThread << std::dec << std::endl;
            return;
        }

        {
            CpuContextScope scope(cpu);
            cpu->gpr[3] = taskThread + kTaskMessageQueueOffset;
            cpu->gpr[4] = outMsgPtr;
            cpu->gpr[5] = 1; // OS_MESSAGE_BLOCK
            InvokeIndirectCpu(0x801DE1BCu, cpu); // OSReceiveMessage
        }

        const uint32_t job = Memory::Read32(outMsgPtr);
        Memory::Write32(taskThread + kTaskCurrentJobOffset, job);

        if (job != 0 && Memory::Contains(job, kJobSize)) {
            const uint32_t callback = Memory::Read32(job + kJobCallbackOffset);
            const uint32_t arg = Memory::Read32(job + kJobArgOffset);
            const uint32_t token = Memory::Read32(job + kJobTokenOffset);
            const uint32_t onDone = Memory::Read32(job + kJobOnDoneOffset);

            if (callback != 0) {
                CpuContextScope scope(cpu);
                if (callback == 0x80529D68u) {
                    RunMovieManagerPrepareAsync(arg, cpu);
                } else {
                    cpu->gpr[3] = arg;
                    InvokeIndirectCpu(callback, cpu);
                }
            }

            const uint32_t currentJob = Memory::Read32(taskThread + kTaskCurrentJobOffset);
            if (currentJob != 0 && onDone != 0) {
                CpuContextScope scope(cpu);
                cpu->gpr[3] = Memory::Read32(currentJob + kJobArgOffset);
                InvokeIndirectCpu(onDone, cpu);
            }

            if (Memory::Contains(taskThread + kTaskDoneQueueOffset, 4)) {
                const uint32_t doneQueue = Memory::Read32(taskThread + kTaskDoneQueueOffset);
                if (doneQueue != 0) {
                    CpuContextScope scope(cpu);
                    cpu->gpr[3] = doneQueue;
                    cpu->gpr[4] = token;
                    cpu->gpr[5] = 0; // OS_MESSAGE_NOBLOCK
                    InvokeIndirectCpu(0x801DE0F4u, cpu); // OSSendMessage
                }
            }
        }

        ClearTaskJob(taskThread, job);
    }
}
#endif  // NSMBW: end of MKW EGG::TaskThread / THP player HLE
#if 0  // NSMBW: MKW EGG::TaskThread / THP movie player; NSMBW has neither
#if 0  // SPM: 0x80242d7c: no NSMBW symbol at this address
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE_VOID(80242D7C, TaskThread_run_HLE_80242d7c, (CpuContext* ctx), (ctx));
#endif
#endif
#endif
