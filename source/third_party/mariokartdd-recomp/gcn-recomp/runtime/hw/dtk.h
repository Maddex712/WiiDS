// DVD audio streaming ("DTK") state shared between the DI (commands) and the mixer.
#pragma once
#include <cstdint>
#include <mutex>

struct DtkState {
    bool playing = false;
    uint64_t start = 0, len = 0, cur = 0;
    uint64_t next_start = 0, next_len = 0;
    bool stop_at_end = false;
    int32_t hist[2][2] = {};  // ADPCM history per channel
};

extern DtkState g_dtk;
extern std::mutex g_dtk_mutex;
