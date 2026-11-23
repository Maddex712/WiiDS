#pragma once

#include <webgpu/webgpu_cpp.h>

namespace aurora::gfx::detail {
struct RenderPass;
struct FramePacket;
}

namespace aurora::gfx::efb_blend6 {

// RGBA6 EFB blending (GX_AURORA_ACCURACY_EFB_BLEND). On the console the blend unit mixes the TEV's
// 8-bit output with the stored pixel and stores the result dithered (when the draw dithers) and
// truncated to 6 bits per channel. A WebGPU fragment shader never sees the destination, so blended
// RGBA6 draws output their 8-bit color and the pass's color target is quantized once the pass has
// ended, before anything reads the EFB (texture copies, peeks, the next pass's blends, the display
// copy). Pixels already holding a 6-bit value (unblended draws, already quantized in their shader)
// keep it, so only blended results change. Approximations: blends inside one pass read the
// unquantized result of earlier blends of that pass (the console truncates after each one), the
// GPU blend rounds where the PE truncates. The quantization dithers per pass: an EFB pass is split
// where the blended draws switch dither state. A pass ended by a copy of at most 64 pixels (peek probes) hands its flags to the
// next pass on the same target. Called after every encoded pass; render worker only.
void after_pass(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame, const detail::RenderPass& pass);
// 0212: a pass whose color nothing reads before the next op is resolved lazily: before_op runs
// it before any other op, unless that op is the next pass on the same target and clears its color
// completely (then the quantized values would be overwritten unseen and it is dropped). flush runs
// what is left at the end of the frame.
void before_op(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame, const detail::RenderPass* next);
void flush(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame);
void initialize();
void shutdown();

} // namespace aurora::gfx::efb_blend6
