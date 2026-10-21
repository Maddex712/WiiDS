#pragma once

#include "dolphin/gx/GXEnum.h"
#include "../internal.hpp"

namespace aurora::gx::fifo {

// Reset the CP write-deduplication state whenever the GX shadow state is reinitialized.
void reset_cp_register_cache();

// Process a buffer of GX FIFO commands
void process(const uint8_t* data, uint32_t size, bool bigEndian);

// Submit already-packed direct vertex bytes against the current GX state.
bool submit_raw_draw(GXPrimitive prim, GXVtxFmt fmt, const uint8_t* vertices, uint16_t vtxCount,
                     uint32_t vertexBytes);
// Same, but as an in-stream draw packet (no worker sync). Caller must have published the matching
// vertex layout through the stream; see the definition.
bool submit_draw_in_stream(GXPrimitive prim, GXVtxFmt fmt, const uint8_t* vertices, uint16_t vtxCount,
                           uint32_t vertexBytes);

} // namespace aurora::gx::fifo
