#pragma once

#include <cstdint>
#include <vector>

namespace ThorWorldMap {
// Capture only from the guest thread. JNI/UI consumers use the copied ReadWords
// result and never inspect guest memory. This module never writes guest memory.
void Capture();

// Schema 1: header 12 words, followed by count * 12 point words.
// Header: schema, sequence, flags, world, submap, count, playerX(float bits),
// playerZ(float bits), minX, maxX, minZ, maxZ (also float bits).
// Flags: 1 = graph available, 2 = player position available. World is zero based
// or -1. An absent player's coordinate words must not be used as a marker.
// Point: original ID, kind (0 path / 1 named W stop), first four name characters
// packed big-endian, fifth character in low byte, x bits, z bits, six linked IDs.
// Missing/invalid links are -1. IDs retain the original 0..191 graph indices.
// Coordinates are top-down world X/Z, independent of the upper-screen camera.
// Nodes deliberately carry no inferred unlocked/completed status.
std::vector<int32_t> ReadWords();
}
