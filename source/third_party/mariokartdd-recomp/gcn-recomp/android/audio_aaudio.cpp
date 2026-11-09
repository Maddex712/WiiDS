// AAudio device output for the portable mix in audio.cpp. Android, including the
// headset; the desktop uses audio_sdl.cpp.
//
// AAudio rather than Oboe: Oboe exists to paper over the broken audio paths on Android
// 4.4-7.x by falling back to OpenSL ES, and this runs on API 29 and up, where Oboe is a
// thin wrapper over exactly the calls below. One file against the platform API is less
// to carry than a third-party dependency fetched at configure time.
#include "runtime.h"
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <thread>

void audio_render(int16_t* out, int frames);
void audio_open_wav();

static constexpr int OUT_RATE = 48000;

static AAudioStream* g_stream;
// Set while a disconnect is being recovered from, so a storm of error callbacks starts
// exactly one rebuild.
static std::atomic<bool> g_restarting{false};
static std::atomic<int> g_callbacks;

static bool stream_start();

// Nothing in here calls back into AAudio. The data callback runs on the stream's own
// thread while that stream is being started and torn down; the API rules out stop,
// pause, close and waitForStateChange from inside it, getState is no better in practice,
// and bumping a counter for another thread to read costs nothing.
static aaudio_data_callback_result_t on_data(AAudioStream*, void*, void* audio_data,
                                             int32_t frames) {
    g_callbacks.fetch_add(1, std::memory_order_relaxed);
    // GCN_AUDIO_TEST=1 fills the buffer here instead of asking for the mix, which
    // separates "the device is not pulling" from "the mix is not returning".
    static const bool test = getenv("GCN_AUDIO_TEST") != nullptr;
    if (test) {
        static double ph;
        int16_t* o = (int16_t*)audio_data;
        for (int32_t i = 0; i < frames; i++) {
            const int16_t v = (int16_t)(6000.0 * sin(ph));
            ph += 2.0 * 3.14159265358979 * 440.0 / OUT_RATE;
            o[2 * i] = o[2 * i + 1] = v;
        }
    } else {
        // The mix is stereo 16-bit at OUT_RATE, which is what the stream was opened as,
        // so this is the whole of the device side: hand over the buffer and fill it.
        audio_render((int16_t*)audio_data, (int)frames);
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// A stream is disconnected when the route changes -- headphones in or out, or on the
// headset, Link being plugged in. The stream is dead from that moment and the only cure
// is to build a new one, which must not happen on this thread: AAudio calls the error
// callback from inside the stream it is tearing down, and closing it there deadlocks.
static void on_error(AAudioStream*, void*, aaudio_result_t err) {
    bool expected = false;
    if (!g_restarting.compare_exchange_strong(expected, true)) return;
    __android_log_print(ANDROID_LOG_INFO, GCN_LOG_TAG, "audio: stream error %s, reopening",
                        AAudio_convertResultToText(err));
    std::thread([] {
        AAudioStream* old = g_stream;
        g_stream = nullptr;
        if (old) AAudioStream_close(old);
        if (!stream_start())
            __android_log_print(ANDROID_LOG_ERROR, GCN_LOG_TAG, "audio: reopen failed");
        g_restarting = false;
    }).detach();
}

// Watches the stream and says something only when there is something to say. The app has
// no environment to set a debug variable in, so this is what answers "is there sound?"
// from a logcat alone: one line when it starts playing, and a line whenever it stops,
// stalls or glitches.
static void watch_stream() {
    std::thread([] {
        int last = 0;
        bool announced = false;
        aaudio_stream_state_t last_state = AAUDIO_STREAM_STATE_UNKNOWN;
        int last_xruns = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            AAudioStream* s = g_stream;
            if (!s) continue;
            const int n = g_callbacks.load(std::memory_order_relaxed);
            const int delta = n - last;
            last = n;
            const aaudio_stream_state_t st = AAudioStream_getState(s);
            const int xruns = (int)AAudioStream_getXRunCount(s);
            if (!announced && st == AAUDIO_STREAM_STATE_STARTED && delta > 0) {
                announced = true;
                __android_log_print(ANDROID_LOG_INFO, GCN_LOG_TAG,
                                    "audio: playing, %d callbacks/s", delta);
            } else if (announced && (delta == 0 || st != last_state || xruns != last_xruns)) {
                // A device that goes to sleep stops consuming and the callbacks stop with
                // it, which is worth saying out loud: it looks exactly like a broken mix.
                __android_log_print(ANDROID_LOG_INFO, GCN_LOG_TAG,
                                    "audio: %d callbacks/s, state %s, xruns %d", delta,
                                    AAudio_convertStreamStateToText(st), xruns);
            }
            last_state = st;
            last_xruns = xruns;
        }
    }).detach();
}

static bool stream_start() {
    AAudioStreamBuilder* b = nullptr;
    aaudio_result_t r = AAudio_createStreamBuilder(&b);
    if (r != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_ERROR, GCN_LOG_TAG, "audio: no stream builder: %s",
                            AAudio_convertResultToText(r));
        return false;
    }
    // GCN_AUDIO_PERF=none asks for the ordinary mixer path rather than the low-latency
    // one, and GCN_AUDIO_BURSTS=N sets the device buffer in bursts (0 leaves AAudio's own
    // choice alone). Both are here because what a given device will actually start
    // playing is not something the documentation settles.
    const char* perf = getenv("GCN_AUDIO_PERF");
    const bool low_latency = !(perf && !strcmp(perf, "none"));
    const int bursts = getenv("GCN_AUDIO_BURSTS") ? atoi(getenv("GCN_AUDIO_BURSTS")) : 2;

    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(b, low_latency ? AAUDIO_PERFORMANCE_MODE_LOW_LATENCY
                                                          : AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, OUT_RATE);
    // What the stream is for. Left unsaid it is attributed as plain media, which on a
    // headset is a different routing and ducking policy than a game's.
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setDataCallback(b, on_data, nullptr);
    AAudioStreamBuilder_setErrorCallback(b, on_error, nullptr);

    AAudioStream* s = nullptr;
    r = AAudioStreamBuilder_openStream(b, &s);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_ERROR, GCN_LOG_TAG, "audio: cannot open device: %s",
                            AAudio_convertResultToText(r));
        return false;
    }

    // The builder's requests are requests. A device that will not give 48 kHz stereo
    // 16-bit would need the mix resampled or downmixed, and nothing this runs on does
    // that, so say what happened rather than playing it at the wrong rate in silence.
    const int32_t rate = AAudioStream_getSampleRate(s);
    const int32_t chans = AAudioStream_getChannelCount(s);
    const aaudio_format_t fmt = AAudioStream_getFormat(s);
    if (rate != OUT_RATE || chans != 2 || fmt != AAUDIO_FORMAT_PCM_I16) {
        __android_log_print(ANDROID_LOG_ERROR, GCN_LOG_TAG,
                            "audio: device gave %d Hz, %d ch, format %d; wanted %d Hz "
                            "stereo 16-bit. Not playing.", rate, chans, (int)fmt, OUT_RATE);
        AAudioStream_close(s);
        return false;
    }

    // Two bursts is the usual starting point for a callback stream: enough that a late
    // callback does not glitch, small enough to stay in the fast path. The mix carries
    // its own cushion against the *guest* being late, which is the delay that actually
    // matters here and is not this buffer's job.
    const int32_t burst = AAudioStream_getFramesPerBurst(s);
    if (bursts > 0 && burst > 0) AAudioStream_setBufferSizeInFrames(s, burst * bursts);

    // Published before requestStart, not after: the data callback can fire the moment
    // the stream starts, and anything it reaches for has to already be there.
    g_stream = s;
    r = AAudioStream_requestStart(s);
    if (r != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_ERROR, GCN_LOG_TAG, "audio: cannot start: %s",
                            AAudio_convertResultToText(r));
        g_stream = nullptr;
        AAudioStream_close(s);
        return false;
    }
    __android_log_print(ANDROID_LOG_INFO, GCN_LOG_TAG,
                        "audio: %d Hz stereo, burst %d, buffer %d, %s latency", rate, burst,
                        AAudioStream_getBufferSizeInFrames(s),
                        AAudioStream_getPerformanceMode(s) == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY
                            ? "low" : "normal");
    return true;
}

bool audio_open() {
    audio_open_wav();
    if (!stream_start()) return false;
    watch_stream();
    return true;
}
