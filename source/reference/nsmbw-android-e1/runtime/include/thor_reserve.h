#pragma once
#include <array>
#include <cstdint>
struct CpuContext;

namespace ThorReserve {
// Original EN_ITEM pickup kinds: 0 mushroom, 1 fire, 4 ice, 11 propeller,
// 13 mini, 14 penguin; -1 means empty. Game/save data is never modified.
// Independent slot per selected Wii save (0..2), persisted in a small sidecar
// beside the resolved game NAND. No original save-file bytes are changed.
void ObserveAcceptedPickup(CpuContext* context);
void Tick(CpuContext* context);
void FilterScore(CpuContext* context); // a caught spare scores nothing
void UpperHud(CpuContext* context);    // DS split: coins and time stay on the upper HUD
bool RequestUse(); // UI thread: queues one request, never calls guest code.
// [schema, sequence, item, canUse, queued, saveSlot, uses, stores]
std::array<int32_t, 8> ReadWords();
}
