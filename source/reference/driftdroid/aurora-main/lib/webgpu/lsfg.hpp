#pragma once

#include "wgpu.hpp"

#include <cstdint>

// Lossless Scaling frame generation for the inserted presentation slots of the frame interpolation
// target (aurora.cpp encode_sealed_frame). The frames are exchanged with libbloodframe.so, which
// runs LSFG 3.1 from the player's own Lossless.dll on its own Vulkan device, as AHardwareBuffers
// synchronised by sync files. Only the encode phase calls the frame functions, one frame at a time.
//
// Per frame: begin_input() hands out the view the real frame (without overlays) is drawn into;
// once that work is submitted, submit_input() runs generation and says how many frames came out;
// each one is drawn into its slot between begin_output(k) and end_output(k), the latter after the
// slot's submit. Any failure turns generation off for the session (status() says why) and the
// slots fall back to repeating the real frame.
namespace aurora::lsfg {

// Any thread. A null or empty path turns generation off.
void configure(const char* dllPath, float flowScale, bool performance) noexcept;
const char* status() noexcept;
bool enabled() noexcept;
// Frames generated per real frame by the last present; 0 while generation is not running.
uint32_t active_frames() noexcept;

// Bracket a frame's generation work, from begin_input() to the last slot's submit: a WebGPU error
// raised inside turns generation off (status() gives Dawn's message) instead of reaching the
// device's fatal uncaptured-error handler.
void push_error_scope() noexcept;
void pop_error_scope() noexcept;

bool begin_input(uint32_t width, uint32_t height, uint32_t slots, wgpu::TextureView& view) noexcept;
uint32_t submit_input(uint32_t slots) noexcept;
wgpu::BindGroup begin_output(uint32_t index) noexcept;
void end_output(uint32_t index) noexcept;

void shutdown() noexcept;

} // namespace aurora::lsfg
