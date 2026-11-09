// The Gekko's page tables, for a game that maps memory with them. See mmu.cpp.
#pragma once
#include <cstdint>

// The page table base register (mtsdr1), from hle_mtspr.
void mmu_set_sdr1(uint32_t v);

// Reads a word of guest memory without disturbing the guest, for host code that watches
// the game (a game project's frame hooks): an address in the page-mapped window is looked
// up in the game's page table, but a missing page is not faulted in (which would run the
// game's own handler under the watcher) and the PTE's R bit is left alone. False if the
// page is not mapped. An address in RAM is read as mem_r32 would.
bool vm_peek32(uint32_t ea, uint32_t* out);
