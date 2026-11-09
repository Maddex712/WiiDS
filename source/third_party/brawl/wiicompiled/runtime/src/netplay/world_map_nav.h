#pragma once

#include <cstdint>

// Clicking the world map's points: the first click on a course walks the player there (D-pad
// presses along the map's routes), a click on the point the player stands on enters it.
namespace WorldMapNav {

// The point of the map at a tap (lx, ly), in layout units (y up, 456 units tall, centred on the
// screen): a course or other place the player can stop on, or -1.
int PointAtTap(float lx, float ly);

// Whether the player stands on `point`.
bool PlayerOn(int point);

// Starts walking the player to `point`; Buttons() then plays the presses.
void WalkTo(int point);

// Once per read of channel 1 while walking: the buttons held now. `active` is false once done.
uint32_t Buttons(bool& active);

void Cancel();

// NSMBW_DUMP_WMAP: logs the map's points, the camera and the player (debugging).
void Dump();

}  // namespace WorldMapNav
