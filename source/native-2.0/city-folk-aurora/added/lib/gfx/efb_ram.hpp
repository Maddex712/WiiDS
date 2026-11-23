#pragma once

// Hardware-accurate EFB copies to main memory ("EFB copies to texture + RAM").
//
// GXCopyTex keeps its fast GPU texture (possibly upscaled), keyed by destination pointer.
// This module adds what the console does besides: the copy's texels exist as GX-format
// bytes (tiled, big-endian) at the destination in main memory.
//
//  * GPU consumers: a texture that reads a copy's memory with a different format, size or
//    offset than the copy (e.g. a Z16 depth copy sampled as IA8) is decoded from those
//    exact bytes, at native resolution, in the same frame (GPU encode + decode, no stall).
//  * CPU consumers: copies into ranges the guest reads with the CPU (DCInvalidateRange, or
//    every copy when readback-all is on) are read back asynchronously and written into
//    memory once the GPU has finished the frame (writeback skipped when the CPU has written
//    the range since the copy).
//  * CPU/DMA writes into a copy's range (detected by hashing the range) drop the copy, so a
//    texture loaded there afterwards comes from memory.

#include "types.hpp"

#include <dolphin/gx/GXEnum.h>

#include <functional>
#include <vector>

struct GXTexObj_;

namespace aurora::gfx::detail {
struct FramePacket;
}

namespace aurora::gfx::efb_ram {

// Work recorded on the FIFO thread and run on the render worker, in pass order.
using PassOp = std::function<void(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame)>;

void initialize();
void shutdown();
// Copies finished readback maps out of their buffers (0219: never inside the map callback). Called
// on the render worker after each frame and by aurora_efb_ram_apply; holds no lock across Dawn calls.
void finish_maps();

bool enabled() noexcept;
bool supports_copy_format(GXTexFmt fmt) noexcept;

// Called by GXCopyTex processing before the EFB pass is resolved into `copy`. Returns work to
// run right after the resolve (encode + readback) or an empty function.
PassOp on_copy(const void* dest, GXTexFmt fmt, uint32_t width, uint32_t height, const TextureHandle& copy);

// Called when a texture is bound. Returns a texture decoded from the copied bytes when `obj`
// reads EFB-copied memory in a way the copy's own texture can't represent; drops copies whose
// memory the CPU has overwritten. Returns an empty handle to use the normal paths.
TextureHandle resolve_texture(const GXTexObj_& obj);

// Frame interpolation (GX_AURORA_COPY_GENERATION / replay state slots, FIFO thread): copies go
// into per-generation byte buffers; while `replaying`, copies are not dropped for CPU writes.
// The tracked copies are saved/restored with the replay state slots.
void set_generation(uint8_t generation, bool replaying, bool noReadback);
void save_slot(uint8_t slot);
void restore_slot(uint8_t slot);

// 0250 (memory growing over a session, LM issue #34): called every 64 frames with the FIFO drained
// (gx::texture::end_frame); the tracked copies are checked against memory as resolve_texture does
// when a texture reads them: a copy whose memory the CPU or DMA has overwritten (the game freed
// and reused it, e.g. the previous map's shadow-map buffer) is dropped and Aurora's copy texture
// for it evicted, even if no texture ever reads that memory again. Byte buffers no tracked copy
// uses any more are released. Returns the destinations to evict (the caller evicts them).
void sweep(std::vector<const void*>& evict);

} // namespace aurora::gfx::efb_ram
