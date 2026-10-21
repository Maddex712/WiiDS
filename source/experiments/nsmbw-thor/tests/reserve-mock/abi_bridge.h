#pragma once
#include <cstdint>
struct CpuContext { uint32_t gpr[32]{}; uint32_t lr=0; };
void InvokeIndirectCpu(uint32_t, CpuContext*);
