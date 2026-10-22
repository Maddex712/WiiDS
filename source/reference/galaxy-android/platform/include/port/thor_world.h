#pragma once
// AYN Thor lower screen: the live state of the stage's placed world pieces (planets, map parts,
// scenery), so the lower screen's 3D diorama shows what the game shows: a piece that a cutscene
// or switch makes appear appears there too, moving and rotating parts move, broken parts go.
//
// Written by the game thread once a frame (decomp/src/Game/System/ThorWorldState.cpp) and read by
// the helper app through GalaxyActivity.nativeWorldState (platform/mobile/thor/ThorLowerBridge.cpp).
// Pieces are keyed as the placement files key them: the zone's name and the object's l_id, which
// is what Bake-LowerPlanets.py records for each baked piece; the members a group object lays out
// itself (a flower group's flowers) follow it with child 1, 2, ... Little-endian, packed as declared.
#include <stdint.h>

#define THOR_WORLD_VERSION 1
#define THOR_WORLD_PIECES 4096
#define THOR_WORLD_ZONES 64
#define THOR_WORLD_ZONE_NAME 48

enum {
    THOR_PIECE_ALIVE = 1,    // the actor exists in the scene now (not dead)
    THOR_PIECE_HIDDEN = 2,   // alive, but its model is hidden
    THOR_PIECE_MATRIX = 4,   // mtx holds its model's base matrix (else its position and rotation)
    THOR_PIECE_CLIPPED = 8,  // outside the upper camera's view (still drawn on the lower screen)
};

typedef struct ThorWorldPiece {
    int16_t zone;        // index into ThorWorldHeader.zones
    uint16_t flags;      // THOR_PIECE_*
    int32_t linkId;      // the placement's l_id
    int16_t child;       // 0: the placed object itself; k: its k-th member
    int16_t reserved;
    float mtx[12];       // world matrix, row-major 3x4, without scale
    float scale[3];
} ThorWorldPiece;        // 72 bytes

typedef struct ThorWorldHeader {
    uint32_t version;
    uint32_t generation;  // changes whenever the set of pieces does (a new scene)
    uint32_t count;       // pieces that follow the header
    uint32_t zoneCount;
    char zones[THOR_WORLD_ZONES][THOR_WORLD_ZONE_NAME];
} ThorWorldHeader;        // 16 + 3072 bytes

#define THOR_WORLD_SIZE (sizeof(ThorWorldHeader) + THOR_WORLD_PIECES * sizeof(ThorWorldPiece))

#ifdef __cplusplus
extern "C" {
#endif
// Copies the latest published world state into [out] (at most [size] bytes); returns its
// sequence number (0 when there is none yet) and leaves [out] untouched when it is unchanged.
uint32_t port_thor_world_copy(uint8_t* out, int size, uint32_t have);
#ifdef __cplusplus
}
#endif
