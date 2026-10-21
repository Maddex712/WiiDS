#include "ios_test_support.h"
#include "hle/net/network.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void IOS_IoctlAsync_80194158(CpuContext*);
extern "C" void IOS_CloseAsync_HLE(CpuContext*);
extern "C" void IOS_ReadAsync_HLE(CpuContext*);
bool NandProcessPendingCallbacks(CpuContext*, int);

namespace {
int checks = 0, calls = 0, callbackDepth = 0, maxCallbackDepth = 0;
int32_t backendResult = -101;
uint32_t currentThread = 0, lastCommand = 0;
bool hasGuestFibers = false, chain = false;
std::array<uint8_t, 64> output{};
std::vector<int32_t> results;
std::vector<uint32_t> arguments;
CpuContext persistent;
NetworkDeferredContract::StartOutcome networkStart{NetworkDeferredContract::StartDisposition::Started, 0};
void Check(bool value, const char* label) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); }
}
CpuContext Request(uint32_t callback = 0x801D1060) {
    CpuContext cpu;
    for (int i=0;i<32;++i) cpu.gpr[i]=0x12340000u+i;
    cpu.gpr[3]=7; cpu.gpr[4]=218; cpu.gpr[5]=0x1000; cpu.gpr[6]=32;
    cpu.gpr[7]=0x2000; cpu.gpr[8]=64; cpu.gpr[9]=callback; cpu.gpr[10]=0xABCD;
    cpu.lr=0x801D0D34; cpu.ctr=0x76543210;
    return cpu;
}
void ResetResults() { results.clear(); arguments.clear(); calls=0; chain=false; }
}

CpuContext& GetPersistentCpuContext() { return persistent; }
void* Memory::GetPointer(uint32_t) { return output.data(); }
uint32_t Fiber::GuestFiberManager::GetCurrentGuestThread() { return currentThread; }
bool Fiber::GuestFiberManager::HasRegisteredGuestThreads() { return hasGuestFibers; }
const void* TranslatedFunctionRegistry::FindByAddressPtr(uint32_t ptr) {
    return ptr == 0x801D1060 || ptr == 0x7001 ? &persistent : nullptr;
}
void LogNandError(const char*, const char*, ...) { std::abort(); }
void InvokeIndirectCpu(uint32_t callback, CpuContext* cpu) {
    ++callbackDepth; maxCallbackDepth=std::max(maxCallbackDepth,callbackDepth);
    ++calls; results.push_back(static_cast<int32_t>(cpu->gpr[3])); arguments.push_back(cpu->gpr[4]);
    if (chain && callback==0x801D1060) {
        cpu->gpr[3]=7; cpu->gpr[4]=0x7001; cpu->gpr[5]=0xCDEF;
        IOS_CloseAsync_HLE(cpu);
        Check(calls==1,"nested IOS completion stays queued inside active real drain");
    }
    cpu->gpr[13]=0xDEADBEEF; cpu->gpr[31]=0xBADF00D;
    cpu->lr=0x11111111; cpu->ctr=0x22222222;
    --callbackDepth;
}

extern "C" int32_t NAND_IOS_Ioctl_HLE(uint32_t,uint32_t cmd,uint32_t,uint32_t,uint32_t,uint32_t) {
    lastCommand=cmd;
    // The backend alone writes output; queue completion must preserve all bytes.
    if (backendResult>=0) for (size_t i=0;i<output.size();++i) output[i]=static_cast<uint8_t>(i ^ 0xA5);
    return backendResult;
}
extern "C" int32_t NAND_IOS_Open_HLE(uint32_t,uint32_t) { return backendResult; }
extern "C" int32_t NAND_IOS_Close_HLE(uint32_t) { return backendResult; }
extern "C" int32_t NAND_IOS_Read_HLE(uint32_t,uint32_t,uint32_t) { return backendResult; }
extern "C" int32_t NAND_IOS_Write_HLE(uint32_t,uint32_t,uint32_t) { return backendResult; }
extern "C" int32_t NAND_IOS_Seek_HLE(uint32_t,int32_t,int32_t) { return backendResult; }
extern "C" int32_t NAND_IOS_Ioctlv_HLE(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) { return backendResult; }
extern "C" bool Network_HLE_IsFd(uint32_t fd) { return fd==999; }
extern "C" int32_t Network_HLE_OpenDevice(const char*,uint32_t) { return 0; }
extern "C" int32_t Network_HLE_Close(uint32_t) { return backendResult; }
extern "C" int32_t Network_HLE_Ioctl(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) { return backendResult; }
extern "C" int32_t Network_HLE_Ioctlv(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) { return backendResult; }
NetworkDeferredContract::StartOutcome Network_HLE_StartIoctlAsync(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) { return networkStart; }
NetworkDeferredContract::StartOutcome Network_HLE_StartIoctlvAsync(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) { return networkStart; }

int main() {
    auto cpu=Request(); auto before=cpu; output.fill(0x5C);
    IOS_IoctlAsync_80194158(&cpu);
    Check(calls==1,"bootstrap error completes before caller can enter spin loop");
    Check(results==std::vector<int32_t>{-101},"backend error reaches callback unchanged");
    Check(arguments==std::vector<uint32_t>{0xABCD},"callback argument preserved");
    Check(cpu.gpr[3]==0,"async launch still reports queued success");
    Check(cpu.gpr[13]==before.gpr[13] && cpu.gpr[31]==before.gpr[31] && cpu.lr==before.lr && cpu.ctr==before.ctr,"real dispatch isolates callback register writes");
    Check(std::all_of(output.begin(),output.end(),[](uint8_t b){return b==0x5C;}),"completion does not invent output on error");
    Check(lastCommand==218,"requested hardware command reaches backend unchanged");
    Check(!NandProcessPendingCallbacks(&cpu,64),"bootstrap callback is consumed exactly once");

    ResetResults(); backendResult=1; cpu=Request(); IOS_IoctlAsync_80194158(&cpu);
    Check(results==std::vector<int32_t>{1},"positive backend result preserved");
    bool bytes=true; for(size_t i=0;i<output.size();++i) bytes &= output[i]==static_cast<uint8_t>(i^0xA5);
    Check(bytes,"backend output bytes preserved exactly");

    ResetResults(); hasGuestFibers=true; currentThread=0x80350000; backendResult=-7;
    cpu=Request(); IOS_IoctlAsync_80194158(&cpu);
    Check(calls==0,"running guest completion remains deferred");
    Check(cpu.gpr[3]==0,"running async call returns queued success");
    Check(NandProcessPendingCallbacks(&cpu,64),"scheduler pump dispatches pending running completion");
    Check(calls==1 && results[0]==-7,"deferred result reaches callback once");
    Check(!NandProcessPendingCallbacks(&cpu,64),"deferred callback is consumed exactly once");

    ResetResults(); currentThread=0; cpu=Request(); IOS_IoctlAsync_80194158(&cpu);
    Check(calls==0,"scheduler context after fibers exist is not bootstrap");
    Check(NandProcessPendingCallbacks(&cpu,64),"scheduler context queued completion remains pumpable");

    ResetResults(); hasGuestFibers=false; chain=true; cpu=Request(); IOS_IoctlAsync_80194158(&cpu);
    Check(calls==2 && results==std::vector<int32_t>({-7,-7}),"outer real drain completes nested queue chain");
    Check(arguments==std::vector<uint32_t>({0xABCD,0xCDEF}),"nested callback keeps own argument");
    Check(maxCallbackDepth==1,"nested completions do not recursively enter guest callbacks");
    Check(!NandProcessPendingCallbacks(&cpu,64),"nested completion leaves no stranded callback");

    ResetResults(); cpu=Request(0); IOS_IoctlAsync_80194158(&cpu);
    Check(calls==0 && !NandProcessPendingCallbacks(&cpu,64),"null callback creates no ghost completion");
    ResetResults(); backendResult=23; cpu=Request(); cpu.gpr[6]=0x801D1060; cpu.gpr[7]=0x777;
    IOS_ReadAsync_HLE(&cpu);
    Check(calls==1 && results[0]==23 && arguments[0]==0x777,"shared completion preserves positive read count");

    ResetResults(); cpu=Request(); cpu.gpr[3]=999; IOS_IoctlAsync_80194158(&cpu);
    Check(calls==0 && !NandProcessPendingCallbacks(&cpu,64),"network worker Started does not manufacture completion");
    ResetResults(); networkStart={NetworkDeferredContract::StartDisposition::ImmediateResult,-55};
    cpu=Request(); cpu.gpr[3]=999; IOS_IoctlAsync_80194158(&cpu);
    Check(calls==1 && results[0]==-55,"network immediate error preserved");
    ResetResults(); networkStart={NetworkDeferredContract::StartDisposition::Unsupported,0}; backendResult=-33;
    cpu=Request(); cpu.gpr[3]=999; IOS_IoctlAsync_80194158(&cpu);
    Check(calls==1 && results[0]==-33,"network fallback result preserved");
    std::printf("PASS: %d actual IOS/async queue checks (mock backend, non-UI ARM64)\n",checks);
}
