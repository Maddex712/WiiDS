#pragma once

// Loads the custom Vulkan driver chosen in the launcher, if any, and routes Dawn's Vulkan backend
// through it. Must run before aurora_initialize. No-op when no driver is selected; falls back to
// the system driver on any failure. See android_gpu_driver.cpp.
#if defined(__ANDROID__) && defined(MKW_HAVE_ADRENOTOOLS)
void LoadAndroidCustomGpuDriver();
#else
inline void LoadAndroidCustomGpuDriver() {}
#endif
