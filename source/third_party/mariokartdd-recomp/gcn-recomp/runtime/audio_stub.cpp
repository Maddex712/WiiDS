// Stands in for audio.cpp in builds that leave SDL out (the benchmark and the headless
// EGL frontend). The rest of the runtime still references these, so they have to exist:
// the DSP HLE hands over mixed sample blocks, and the DI reads and writes the DVD
// streaming state while servicing the game's audio-stream commands.
#include "hw/dtk.h"
#include <cstdint>
#include <mutex>

void audio_push_dma(const int16_t*, uint32_t) {}

DtkState g_dtk;
std::mutex g_dtk_mutex;
