#include "common_rel.h"
#include "dol_tables.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" {
#define CODEREF(addr, name) void name(void);
#include "main_program_functions.inc"
#undef CODEREF
#define RELGLOBAL(addr, name) extern void* name;
#include "relglobal_vars.inc"
#undef RELGLOBAL
#define VARREF(addr, size, name) extern unsigned char name[];
#include "main_program_variables.inc"
#undef VARREF
extern unsigned char relglobal_unk270[0x1BFC];
}

namespace {
const XDMainProgramAddress kMap[] = {
#define CODEREF(addr, name) {addr, 1, reinterpret_cast<void*>(&name)},
#include "main_program_functions.inc"
#undef CODEREF
#define RELGLOBAL(addr, name) {addr, 4, &name},
#include "relglobal_vars.inc"
#undef RELGLOBAL
#define VARREF(addr, size, name) {addr, size, name},
#include "main_program_variables.inc"
#undef VARREF
    {0x804E8A38, 0x1BFC, relglobal_unk270},
};
constexpr size_t kMapCount = sizeof(kMap) / sizeof(kMap[0]);

std::vector<const XDMainProgramAddress*> sSorted;

uintptr_t dol_host_address(uint32_t address) {
#if defined(__ANDROID__)
  // The Android DOL loader writes into the relocated MEM1 region.
  return XdMem1ToHost(address);
#else
  return address;
#endif
}

uintptr_t dol_guest_address(const void* pointer) {
  const auto address = reinterpret_cast<uintptr_t>(pointer);
#if defined(__ANDROID__)
  return XdMem1ToGuest(address);
#else
  return address;
#endif
}

struct ScriptBlock {
  uint32_t gc;
  unsigned char* pc;
  uint32_t size;
};
std::vector<ScriptBlock> sScriptBlocks;
const ScriptBlock* FindScriptBlock(uint32_t gc) {
  for (const ScriptBlock& b : sScriptBlocks) {
    if (gc >= b.gc && gc - b.gc < b.size) return &b;
  }
  return nullptr;
}
const ScriptBlock* FindScriptBlockPc(const void* p) {
  const auto* c = static_cast<const unsigned char*>(p);
  for (const ScriptBlock& b : sScriptBlocks) {
    if (c >= b.pc && c < b.pc + b.size) return &b;
  }
  return nullptr;
}

[[noreturn]] void fatal_address(uint32_t gcAddress, const char* why) {
  std::fprintf(stderr, "[games/xd] Game data used GameCube address %08X: %s. Stopping here.\n", gcAddress, why);
  std::fflush(stderr);
  std::abort();
}
}

extern "C" void PortXD_SetScriptBlockSink(void (*sink)(uint32_t, void*, uint32_t));
extern "C" void PortXD_AddScriptBlock(uint32_t gcAddress, void* bytes, uint32_t size);

extern "C" void PortXD_RegisterMainProgramMap(void) {
  PortXD_SetScriptBlockSink(&PortXD_AddScriptBlock);
  PortXD_SetMainProgramMap(kMap, kMapCount);
  sSorted.clear();
  for (const auto& m : kMap) {
    sSorted.push_back(&m);
  }
  std::sort(sSorted.begin(), sSorted.end(),
            [](const XDMainProgramAddress* a, const XDMainProgramAddress* b) { return a->gcAddress < b->gcAddress; });
}

extern "C" void PortXD_AddScriptBlock(uint32_t gcAddress, void* bytes, uint32_t size) {
  for (ScriptBlock& b : sScriptBlocks) {
    if (b.gc == gcAddress) {
      b = {gcAddress, static_cast<unsigned char*>(bytes), size};
      return;
    }
  }
  sScriptBlocks.push_back({gcAddress, static_cast<unsigned char*>(bytes), size});
}

extern "C" void* PortXD_GameAddress(uint32_t gcAddress) {
  if (gcAddress == 0) {
    return nullptr;
  }
  if (!sScriptBlocks.empty()) {
    if (const ScriptBlock* b = FindScriptBlock(gcAddress)) {
      return b->pc + (gcAddress - b->gc);
    }
  }
  auto it = std::upper_bound(sSorted.begin(), sSorted.end(), gcAddress,
                             [](uint32_t a, const XDMainProgramAddress* m) { return a < m->gcAddress; });
  if (it != sSorted.begin()) {
    const XDMainProgramAddress* m = *(it - 1);
    if (gcAddress - m->gcAddress < m->size) {
      return static_cast<unsigned char*>(m->pc) + (gcAddress - m->gcAddress);
    }
  }
  uint32_t bssStart, bssEnd, imageEnd;
  PortXD_DolImage(&bssStart, &bssEnd, &imageEnd);
  if (imageEnd == 0) {
    fatal_address(gcAddress, "main.dol isn't loaded into GameCube memory");
  }
  if (gcAddress >= bssStart && gcAddress < bssEnd) {
    fatal_address(gcAddress, "it's a game variable the port doesn't map yet (add it to "
                             "games/xd/src/data/main_program_variables.inc; name from the decomp's symbols.txt)");
  }
  if (gcAddress < 0x80003100u || gcAddress >= imageEnd) {
    fatal_address(gcAddress, "it's outside main.dol");
  }
  return reinterpret_cast<void*>(dol_host_address(gcAddress));
}

extern "C" void* PortXD_GameAddressIfKnown(uint32_t gcAddress, uint32_t* size) {
  *size = 0;
  if (gcAddress == 0) return nullptr;
  if (const ScriptBlock* b = FindScriptBlock(gcAddress)) {
    *size = b->size - (gcAddress - b->gc);
    return b->pc + (gcAddress - b->gc);
  }
  auto it = std::upper_bound(sSorted.begin(), sSorted.end(), gcAddress,
                             [](uint32_t a, const XDMainProgramAddress* m) { return a < m->gcAddress; });
  if (it != sSorted.begin()) {
    const XDMainProgramAddress* m = *(it - 1);
    if (gcAddress - m->gcAddress < m->size) {
      *size = m->size - (gcAddress - m->gcAddress);
      return static_cast<unsigned char*>(m->pc) + (gcAddress - m->gcAddress);
    }
  }
  uint32_t bssStart, bssEnd, imageEnd;
  PortXD_DolImage(&bssStart, &bssEnd, &imageEnd);
  if (imageEnd == 0 || (gcAddress >= bssStart && gcAddress < bssEnd) || gcAddress < 0x80003100u ||
      gcAddress >= imageEnd) {
    return nullptr;
  }
  *size = imageEnd - gcAddress;
  return reinterpret_cast<void*>(dol_host_address(gcAddress));
}

extern "C" uint32_t PortXD_GameAddressOf(const void* p) {
  if (p == nullptr) {
    return 0;
  }
  if (const ScriptBlock* sb = FindScriptBlockPc(p)) {
    return sb->gc + static_cast<uint32_t>(static_cast<const unsigned char*>(p) - sb->pc);
  }
  const auto* b = static_cast<const unsigned char*>(p);
  for (const XDMainProgramAddress* m : sSorted) {
    const auto* start = static_cast<const unsigned char*>(m->pc);
    if (b >= start && b < start + m->size) {
      return m->gcAddress + static_cast<uint32_t>(b - start);
    }
  }
  uint32_t bssStart, bssEnd, imageEnd;
  PortXD_DolImage(&bssStart, &bssEnd, &imageEnd);
  const auto u = dol_guest_address(p);
  if (u >= 0x80003100u && u < imageEnd) {
    return static_cast<uint32_t>(u);
  }
  std::fprintf(stderr, "[games/xd] PortXD_GameAddressOf(%p): not something game data can refer to. Stopping here.\n", p);
  std::fflush(stderr);
  std::abort();
}
