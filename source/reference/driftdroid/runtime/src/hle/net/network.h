#pragma once

#include "hle/network_deferred_contract.h"

#include <cstdint>

struct CpuContext;

extern "C" int32_t Network_HLE_OpenDevice(const char* path, uint32_t mode);
extern "C" bool Network_HLE_IsFd(uint32_t fd);
extern "C" int32_t Network_HLE_Close(uint32_t fd);
extern "C" int32_t Network_HLE_Ioctl(uint32_t fd, uint32_t cmd, uint32_t inBuf, uint32_t inLen,
                                      uint32_t outBuf, uint32_t outLen);
extern "C" int32_t Network_HLE_Ioctlv(uint32_t fd, uint32_t cmd, uint32_t numIn, uint32_t numOut,
                                       uint32_t vectorPtr);

// DNS can block for seconds, so these copy guest input and resolve on a worker.
// Network_HLE_ProcessCompletions must be pumped from the guest scheduler thread,
// since that is where guest output is committed and the waiter is woken.
NetworkDeferredContract::StartOutcome Network_HLE_StartIoctlSync(
    uint32_t fd, uint32_t cmd, uint32_t inBuf, uint32_t inLen,
    uint32_t outBuf, uint32_t outLen, uint32_t waitQueue);
NetworkDeferredContract::StartOutcome Network_HLE_StartIoctlvSync(
    uint32_t fd, uint32_t cmd, uint32_t numIn, uint32_t numOut,
    uint32_t vectorPtr, uint32_t waitQueue);
NetworkDeferredContract::StartOutcome Network_HLE_StartIoctlAsync(
    uint32_t fd, uint32_t cmd, uint32_t inBuf, uint32_t inLen,
    uint32_t outBuf, uint32_t outLen, uint32_t callback, uint32_t callbackArg);
NetworkDeferredContract::StartOutcome Network_HLE_StartIoctlvAsync(
    uint32_t fd, uint32_t cmd, uint32_t numIn, uint32_t numOut,
    uint32_t vectorPtr, uint32_t callback, uint32_t callbackArg);
bool Network_HLE_TakeSyncResult(uint64_t token, int32_t* result);
bool Network_HLE_ProcessCompletions(CpuContext* cpu);

// True while at least one deferred (non-blocking) connect is still awaiting its outcome.
// Lets a caller distinguish "waiting on a real
// in-flight connect" from every other short poll the same retry idiom is reused for, so it
// only pays a real-time pacing cost where one is actually needed.
bool Network_HLE_HasPendingConnect();
