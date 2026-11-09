// SDL device output for the portable mix in audio.cpp. Desktop only; Android uses
// audio_aaudio.cpp, which is the same handful of lines against a different API.
#include "runtime.h"
#include <SDL.h>
#include <cstdio>
#include <string>

void audio_render(int16_t* out, int frames);
void audio_open_wav();

static constexpr int OUT_RATE = 48000;
static SDL_AudioDeviceID g_dev;
static std::string g_device;  // empty: the system's default

// A device by the name SDL lists it under, for a headset whose runtime names its own.
void audio_set_device(const char* name) { g_device = name ? name : ""; }

static void audio_callback(void*, uint8_t* stream, int len) {
    audio_render((int16_t*)stream, len / 4);
}

bool audio_open() {
    audio_open_wav();
    SDL_AudioSpec want{}, have{};
    want.freq = OUT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = audio_callback;
    g_dev = 0;
    if (const char* e = getenv("GCN_AUDIO_DEVICE"); e && *e) g_device = e;
    if (!g_device.empty()) {
        g_dev = SDL_OpenAudioDevice(g_device.c_str(), 0, &want, &have, 0);
        if (g_dev) fprintf(stderr, "audio: %s\n", g_device.c_str());
        else fprintf(stderr, "audio: cannot open %s (%s); using the default\n", g_device.c_str(), SDL_GetError());
    }
    if (!g_dev) {
        g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        char* name = nullptr;
        SDL_AudioSpec spec;
        if (g_dev && SDL_GetDefaultAudioInfo(&name, &spec, 0) == 0 && name) {
            LOG(LOG_AI, "audio: the default device, %s", name);
            SDL_free(name);
        }
    }
    if (!g_dev) {
        LOG(LOG_AI, "audio: cannot open device: %s", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(g_dev, 0);
    return true;
}
