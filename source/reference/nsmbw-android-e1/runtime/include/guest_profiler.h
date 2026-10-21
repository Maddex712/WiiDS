#pragma once

// NSMBW_PROFILE=<start s>:<length s>: sample the game thread and log where its time goes, by
// translated guest function (guest_profiler.cpp; Windows only).
namespace GuestProfiler {
// From the thread that presents frames, at each present: the first call starts the sampler.
void OnFramePresented();
} // namespace GuestProfiler
