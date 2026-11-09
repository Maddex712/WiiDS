#pragma once

#include "common.hpp"
#include "texture.hpp"

#include <dolphin/gx/GXEnum.h>

namespace aurora::gfx::efb_ram {

inline constexpr size_t MaxAsyncReadbackSlots = 32;

// strideWidth: a copy into part of a wider texture (the destination width set larger than what is
// copied, e.g. NSMBW's animated tiles patching the 1024x1024 tile atlas): its block rows land that
// texture's row pitch apart. These are read back every frame so the texture sees them.
void schedule(void* dest, uint32_t width, uint32_t height, GXTexFmt format, TextureHandle texture,
              uint32_t strideWidth = 0) noexcept;
bool has_pending(void* dest = nullptr) noexcept;
bool prepare_downloads(void* dest = nullptr);
void encode_downloads(const wgpu::CommandEncoder& encoder, void* dest = nullptr) noexcept;
bool complete_downloads() noexcept;
void cancel() noexcept;

// Frame-latent readbacks for probe-sized CPU-consumed copies: they ride the frame's own encode and
// land in guest RAM a frame later. Per frame, from the worker: seal, encode, then after_submit.
void seal_async_downloads() noexcept;
void encode_async_downloads(const wgpu::CommandEncoder& encoder) noexcept;
void after_submit() noexcept;
// Drops requests sealed for a frame that will never be encoded.
void abort_async() noexcept;
void shutdown() noexcept;

} // namespace aurora::gfx::efb_ram
