#pragma once
// 0196: realistic flashlight (include/aurora/enhanced.h `realisticFlashlight`). The angular
// profile of a reflector flashlight, shared by the surface lighting (lib/gx/shader.cpp, spot
// color channels) and the light-scattering pass (lib/gfx/enhanced.cpp), so the shafts in the air
// and the pool on the walls have exactly the same shape.
#include <string_view>

namespace aurora::gfx {
inline constexpr std::string_view BeamProfileWgsl = R"""(
// Cosine at which the GX spot polynomial a0 + a1 c + a2 c^2 reaches 0 (the cone's edge), or -2
// when it has no such edge (not a spot cone).
fn enh_beam_cut(ca: vec3f) -> f32 {
  if (abs(ca.z) < 1e-6) {
    if (abs(ca.y) < 1e-6) { return -2.0; }
    let r = -ca.x / ca.y;
    return select(-2.0, r, r > -1.0 && r < 0.9999);
  }
  let disc = ca.y * ca.y - 4.0 * ca.z * ca.x;
  if (disc < 0.0) { return -2.0; }
  let s = sqrt(disc);
  let r1 = (-ca.y + s) / (2.0 * ca.z);
  let r2 = (-ca.y - s) / (2.0 * ca.z);
  return max(select(-2.0, r1, r1 > -1.0 && r1 < 0.9999), select(-2.0, r2, r2 > -1.0 && r2 < 0.9999));
}
// Intensity across the beam, x = angle from the axis / the cone's half angle (1 = the game's cone
// edge): a hot centre from the bulb's image, a slightly darker ring around it, the reflector's
// even corona with faint concentric rings (`rings` 0..1) and a soft edge, and a dim spill just
// outside the cone. 1.65 on the axis, ~0.75 across the corona, ~0.1 at the edge.
fn enh_beam_profile(x: f32, rings: f32) -> f32 {
  if (x >= 1.45) { return 0.0; }
  let hot = exp(-x * x * 9.0);
  let corona = 1.0 - smoothstep(0.62, 1.02, x);
  let dip = 1.0 - 0.2 * exp(-(x - 0.48) * (x - 0.48) * 90.0);
  let ripple = 1.0 + rings * 0.06 * sin(x * 26.0) * corona;
  let spill = (1.0 - smoothstep(0.9, 1.45, x)) * 0.07;
  return (hot * 0.9 + corona * 0.75 * dip) * ripple + spill;
}
)""";
} // namespace aurora::gfx
