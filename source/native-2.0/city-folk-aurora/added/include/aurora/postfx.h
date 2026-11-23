#ifndef AURORA_POSTFX_H
#define AURORA_POSTFX_H

// Optional post-process chain applied to the game image when it is presented
// (after the EFB/XFB is final, before ImGui is drawn on top, so UI stays crisp).
//
// A chain is one WGSL module plus up to AURORA_POSTFX_MAX_PASSES fullscreen passes.
// Aurora prepends a prelude to the module that declares:
//
//   struct AuroraPostFx {
//     output_size: vec2f,   // this pass's render target size in pixels
//     source_size: vec2f,   // game frame size in pixels (render resolution)
//     viewport: vec4f,      // present viewport in surface pixels (x, y, w, h)
//     surface_size: vec2f,  // swapchain size in pixels
//     frame: u32,           // increments once per presented frame
//   };
//   @group(0) @binding(0) var<uniform> aurora_postfx: AuroraPostFx;
//   @group(0) @binding(2) var aurora_linear: sampler;      // bilinear, clamp to edge
//   struct AuroraPostFxVertex { @builtin(position) position: vec4f, @location(0) uv: vec2f };
//   @vertex fn aurora_postfx_vs(...)                        // fullscreen triangle, uv 0..1 over the target
//
// The module itself declares the user uniform block at @binding(1) (contents set with
// aurora_postfx_set_uniforms) and the pass inputs as texture_2d<f32> at @binding(3 + i).
// The last pass renders into the present viewport of the swapchain (surface format,
// write gamma-encoded values); its @builtin(position) is in surface pixels.
// Earlier passes render into intermediate textures sized by `width`/`height`.

#ifdef __cplusplus
#include <cstdint>
extern "C" {
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#define AURORA_POSTFX_MAX_PASSES 16
#define AURORA_POSTFX_MAX_INPUTS 4
#define AURORA_POSTFX_MAX_UNIFORM_SIZE 4096

typedef enum {
  AURORA_POSTFX_SIZE_VIEWPORT = 0, // value * present viewport extent
  AURORA_POSTFX_SIZE_SOURCE = 1,   // value * game frame extent
  AURORA_POSTFX_SIZE_INPUT = 2,    // value * extent of the pass's first input
  AURORA_POSTFX_SIZE_ABSOLUTE = 3, // value pixels
  AURORA_POSTFX_SIZE_ASPECT = 4,   // value * other axis * game frame aspect (keeps the source aspect)
} AuroraPostFxSizeMode;

typedef struct {
  AuroraPostFxSizeMode mode;
  float value;
} AuroraPostFxSize;

typedef struct {
  const char* entryPoint;     // fragment entry point in the chain's module
  AuroraPostFxSize width;     // ignored for the last pass
  AuroraPostFxSize height;    // ignored for the last pass
  bool hdr;                   // RGBA16Float target instead of the surface format (ignored for the last pass)
  uint32_t inputCount;        // number of textures bound at @binding(3)..
  uint32_t inputs[AURORA_POSTFX_MAX_INPUTS]; // 0 = game frame, n = output of pass n-1
} AuroraPostFxPass;

// Replaces the chain. Passing passCount 0 (or wgsl NULL) disables post-processing.
// The module is compiled on the render thread at the next present; when compilation
// or validation fails the error is logged and the chain is disabled until replaced.
// Calling again with an identical chain is cheap (nothing is rebuilt).
void aurora_postfx_set_chain(const char* wgsl, const AuroraPostFxPass* passes, uint32_t passCount);
// Contents of the user uniform block (@binding(1)), shared by all passes.
void aurora_postfx_set_uniforms(const void* data, uint32_t size);
// True while a chain is set and has not failed to compile.
bool aurora_postfx_active();

#ifdef __cplusplus
}
#endif

#endif
