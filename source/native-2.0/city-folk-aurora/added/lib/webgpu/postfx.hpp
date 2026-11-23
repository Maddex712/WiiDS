#pragma once

#include "gpu.hpp"

namespace aurora::webgpu::postfx {
// Render worker only. Runs the intermediate passes of the post-process chain into
// `encoder`; returns false when no chain is active (present the frame normally).
bool prepare(const wgpu::CommandEncoder& encoder, const Viewport& viewport);
// Draws the chain's last pass into `pass` (viewport/scissor already set to the present viewport).
void draw(const wgpu::RenderPassEncoder& pass);
void shutdown();
} // namespace aurora::webgpu::postfx
