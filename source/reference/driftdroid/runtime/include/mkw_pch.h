// Precompiled header for WiiCompiled
// This header is precompiled to speed up builds
// All common headers used by generated functions should be included here

#ifndef MKW_RECOMPILED_PCH_H
#define MKW_RECOMPILED_PCH_H

// Standard C++ headers
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iostream>

// Everything the translated code and the runtime reference is linked into the one static NRO, so
// no project symbol needs default visibility. Under -fPIC, default visibility makes every global
// access (the page tables, g_currentCpuContext, ...) go through a GOT load the linker does not
// relax; hidden turns them into direct adrp+add. Measured on one shard: 1484 -> 11 GOT relocations.
// Pushed after the system headers above so libc/libstdc++ declarations keep their own settings.
#if defined(__SWITCH__)
#pragma GCC visibility push(hidden)
#endif

// Project headers
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#endif // MKW_RECOMPILED_PCH_H
