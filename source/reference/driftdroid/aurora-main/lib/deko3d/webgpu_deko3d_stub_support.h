#pragma once

namespace aurora::deko3d {
// Aborts with a clear message naming the function, so a call into a not-yet-implemented part of
// the webgpu.h shim shows up immediately instead of misbehaving silently (returning nullptr from
// e.g. wgpuDeviceCreateBuffer would otherwise just null-deref somewhere downstream with no
// indication of why). Real implementations replace individual stubs incrementally - see
// webgpu_deko3d_core.cpp for the ones already done, and gen_webgpu_stubs.py's
// IMPLEMENTED_ELSEWHERE set for how a stub gets excluded once it has a real implementation.
[[noreturn]] void webgpu_stub_unimplemented(const char* functionName);
} // namespace aurora::deko3d
