#include "music_player.h"

#include "music_attenuation.h"
#include "runtime_config.h"

#include <SDL3/SDL.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD  // one code path on every platform; a single stream is cheap either way
#include <minimp3/minimp3.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <random>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

namespace MusicPlayer {
namespace {

constexpr const char* kSettingsFile = ".player";
constexpr size_t kMaxFileBytes = 64u << 20;

struct Track {
    std::vector<uint8_t> data;
    size_t cursor = 0;        // next byte to decode
    size_t start = 0;         // first audio byte (after an ID3 tag)
    int hz = 0;
    int channels = 0;
    uint64_t totalFrames = 0;    // sample frames in the whole file
    uint64_t decodedFrames = 0;  // sample frames handed to SDL so far
    mp3dec_t decoder{};
};

std::mutex g_mutex;
std::condition_variable g_wake;
std::vector<std::string> g_tracks;  // names without extension
bool g_scanned = false;
bool g_settingsLoaded = false;

// Requests from the UI, picked up by the worker.
int g_requestIndex = -1;
bool g_requestStop = false;

// Owned by the worker once loaded; read by the UI under the mutex.
Track g_track;
bool g_loaded = false;
bool g_paused = false;
int g_index = -1;

float g_volume = 0.8f;
bool g_shuffle = false;
bool g_muteGameMusic = true;

SDL_AudioStream* g_stream = nullptr;
bool g_workerStarted = false;

fs::path Folder() { return RuntimeConfigFile::ApplicationDataDirectory() / "Music"; }

void LoadSettingsLocked() {
    if (g_settingsLoaded) {
        return;
    }
    g_settingsLoaded = true;
    if (FILE* file = std::fopen((Folder() / kSettingsFile).string().c_str(), "rb")) {
        int volume = 80, shuffle = 0, mute = 1;
        if (std::fscanf(file, "%d %d %d", &volume, &shuffle, &mute) == 3) {
            g_volume = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f;
            g_shuffle = shuffle != 0;
            g_muteGameMusic = mute != 0;
        }
        std::fclose(file);
    }
}

void SaveSettingsLocked() {
    if (FILE* file = std::fopen((Folder() / kSettingsFile).string().c_str(), "wb")) {
        std::fprintf(file, "%d %d %d\n", static_cast<int>(g_volume * 100.0f + 0.5f), g_shuffle ? 1 : 0,
                     g_muteGameMusic ? 1 : 0);
        std::fclose(file);
    }
}

void UpdateGameMusicLocked() {
    MusicAttenuation::SetMusicPlayerActive(g_loaded && !g_paused && g_muteGameMusic);
}

std::vector<std::string> ScanLocked() {
    std::vector<std::string> names;
    std::error_code ec;
    fs::create_directories(Folder(), ec);
    for (fs::directory_iterator it(Folder(), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".mp3") {
            names.push_back(it->path().filename().string());
        }
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
            return std::tolower(x) < std::tolower(y);
        });
    });
    g_tracks = names;
    g_scanned = true;
    return names;
}

// Reads and measures a file. Runs on the worker without the mutex held.
bool LoadFile(const fs::path& path, Track& track) {
    FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0 || static_cast<size_t>(size) > kMaxFileBytes) {
        std::fclose(file);
        return false;
    }
    track.data.resize(static_cast<size_t>(size));
    const bool ok = std::fread(track.data.data(), 1, track.data.size(), file) == track.data.size();
    std::fclose(file);
    if (!ok) {
        return false;
    }
    // Skip an ID3v2 tag (it can hold a whole cover picture).
    const uint8_t* d = track.data.data();
    if (track.data.size() > 10 && std::memcmp(d, "ID3", 3) == 0) {
        const size_t tag = (static_cast<size_t>(d[6] & 0x7F) << 21) | (static_cast<size_t>(d[7] & 0x7F) << 14) |
                           (static_cast<size_t>(d[8] & 0x7F) << 7) | static_cast<size_t>(d[9] & 0x7F);
        track.start = std::min(track.data.size(), tag + 10);
    }
    // One pass over the frame headers gives the format and the length.
    mp3dec_init(&track.decoder);
    for (size_t at = track.start; at < track.data.size();) {
        mp3dec_frame_info_t info{};
        const int samples = mp3dec_decode_frame(&track.decoder, d + at, static_cast<int>(track.data.size() - at), nullptr, &info);
        if (info.frame_bytes <= 0) {
            break;
        }
        at += static_cast<size_t>(info.frame_bytes);
        if (samples > 0) {
            if (track.hz == 0) {
                track.hz = info.hz;
                track.channels = info.channels;
            }
            track.totalFrames += static_cast<uint64_t>(samples);
        }
    }
    mp3dec_init(&track.decoder);
    track.cursor = track.start;
    return track.hz > 0 && track.channels > 0 && track.totalFrames > 0;
}

int PickNextLocked(int direction) {
    const int count = static_cast<int>(g_tracks.size());
    if (count == 0) {
        return -1;
    }
    if (g_shuffle && count > 1) {
        static std::mt19937 rng{std::random_device{}()};
        int pick = g_index;
        while (pick == g_index) {
            pick = static_cast<int>(rng() % static_cast<unsigned>(count));
        }
        return pick;
    }
    return ((g_index < 0 ? 0 : g_index + direction) % count + count) % count;
}

void Worker() {
    std::vector<int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
    std::unique_lock lock(g_mutex);
    for (;;) {
        g_wake.wait_for(lock, std::chrono::milliseconds(g_loaded && !g_paused ? 20 : 250));

        if (g_requestStop) {
            g_requestStop = false;
            g_requestIndex = -1;
            g_loaded = false;
            g_index = -1;
            g_track = Track{};
            if (g_stream != nullptr) {
                SDL_ClearAudioStream(g_stream);
                SDL_PauseAudioStreamDevice(g_stream);
            }
            UpdateGameMusicLocked();
            continue;
        }

        if (g_requestIndex >= 0) {
            const int index = g_requestIndex;
            g_requestIndex = -1;
            if (index >= static_cast<int>(g_tracks.size())) {
                continue;
            }
            const fs::path path = Folder() / g_tracks[static_cast<size_t>(index)];
            Track track;
            lock.unlock();
            const bool ok = LoadFile(path, track);
            lock.lock();
            if (!ok) {
                // Unreadable or not really an MP3: move on rather than stop the music.
                g_index = index;
                if (g_tracks.size() > 1 && g_requestIndex < 0) {
                    g_requestIndex = PickNextLocked(1);
                }
                continue;
            }
            SDL_AudioSpec spec{};
            spec.format = SDL_AUDIO_S16LE;
            spec.channels = track.channels;
            spec.freq = track.hz;
            if (g_stream == nullptr) {
                if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                    continue;
                }
                g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
                if (g_stream == nullptr) {
                    continue;
                }
            } else {
                SDL_ClearAudioStream(g_stream);
                SDL_SetAudioStreamFormat(g_stream, &spec, nullptr);
            }
            SDL_SetAudioStreamGain(g_stream, g_volume);
            SDL_ResumeAudioStreamDevice(g_stream);
            g_track = std::move(track);
            g_loaded = true;
            g_paused = false;
            g_index = index;
            UpdateGameMusicLocked();
        }

        if (!g_loaded || g_paused || g_stream == nullptr) {
            continue;
        }
        // Keep about half a second queued.
        const int bytesPerSecond = g_track.hz * g_track.channels * 2;
        while (SDL_GetAudioStreamQueued(g_stream) < bytesPerSecond / 2 && g_track.cursor < g_track.data.size()) {
            mp3dec_frame_info_t info{};
            const int samples =
                mp3dec_decode_frame(&g_track.decoder, g_track.data.data() + g_track.cursor,
                                    static_cast<int>(g_track.data.size() - g_track.cursor), pcm.data(), &info);
            if (info.frame_bytes <= 0) {
                g_track.cursor = g_track.data.size();
                break;
            }
            g_track.cursor += static_cast<size_t>(info.frame_bytes);
            // A stray frame in another format (rare, damaged files) would play at the wrong speed.
            if (samples > 0 && info.hz == g_track.hz && info.channels == g_track.channels) {
                SDL_PutAudioStreamData(g_stream, pcm.data(), samples * info.channels * 2);
                g_track.decodedFrames += static_cast<uint64_t>(samples);
            }
        }
        if (g_track.cursor >= g_track.data.size() && SDL_GetAudioStreamQueued(g_stream) == 0 && g_requestIndex < 0) {
            g_requestIndex = PickNextLocked(1);  // the track ended
        }
    }
}

void EnsureWorkerLocked() {
    LoadSettingsLocked();
    if (!g_workerStarted) {
        g_workerStarted = true;
        // Never joined: it runs for the life of the process (and a thread cannot be detached on Switch).
        new std::thread(Worker);
    }
}

std::string DisplayName(const std::string& file) {
    const size_t dot = file.rfind('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}

}  // namespace

std::string FolderForDisplay() {
#if defined(__SWITCH__)
    return "/switch/WiiCompiled/Music";
#else
    return Folder().string();
#endif
}

std::vector<std::string> Rescan() {
    std::lock_guard lock(g_mutex);
    LoadSettingsLocked();
    // Keep pointing at the track that is playing if the list changed around it.
    const std::string current = g_index >= 0 && g_index < static_cast<int>(g_tracks.size()) ? g_tracks[static_cast<size_t>(g_index)] : "";
    std::vector<std::string> names = ScanLocked();
    if (!current.empty()) {
        const auto it = std::find(names.begin(), names.end(), current);
        g_index = it == names.end() ? -1 : static_cast<int>(it - names.begin());
    }
    for (std::string& name : names) {
        name = DisplayName(name);
    }
    return names;
}

std::vector<std::string> Tracks() {
    {
        std::lock_guard lock(g_mutex);
        if (g_scanned) {
            std::vector<std::string> names = g_tracks;
            for (std::string& name : names) {
                name = DisplayName(name);
            }
            return names;
        }
    }
    return Rescan();
}

void Play(int index) {
    std::lock_guard lock(g_mutex);
    if (index < 0 || index >= static_cast<int>(g_tracks.size())) {
        return;
    }
    EnsureWorkerLocked();
    g_requestIndex = index;
    g_wake.notify_all();
}

void TogglePause() {
    std::lock_guard lock(g_mutex);
    if (!g_loaded) {
        if (!g_tracks.empty()) {
            EnsureWorkerLocked();
            g_requestIndex = g_shuffle ? PickNextLocked(1) : 0;
            g_wake.notify_all();
        }
        return;
    }
    g_paused = !g_paused;
    if (g_stream != nullptr) {
        if (g_paused) {
            SDL_PauseAudioStreamDevice(g_stream);
        } else {
            SDL_ResumeAudioStreamDevice(g_stream);
        }
    }
    UpdateGameMusicLocked();
    g_wake.notify_all();
}

void Next() {
    std::lock_guard lock(g_mutex);
    if (g_tracks.empty()) {
        return;
    }
    EnsureWorkerLocked();
    g_requestIndex = PickNextLocked(1);
    g_wake.notify_all();
}

void Previous() {
    std::lock_guard lock(g_mutex);
    if (g_tracks.empty()) {
        return;
    }
    EnsureWorkerLocked();
    g_requestIndex = PickNextLocked(-1);
    g_wake.notify_all();
}

void Stop() {
    std::lock_guard lock(g_mutex);
    if (g_workerStarted) {
        g_requestStop = true;
        g_wake.notify_all();
    }
}

float Volume() {
    std::lock_guard lock(g_mutex);
    LoadSettingsLocked();
    return g_volume;
}

void SetVolume(float volume) {
    std::lock_guard lock(g_mutex);
    LoadSettingsLocked();
    g_volume = std::clamp(volume, 0.0f, 1.0f);
    if (g_stream != nullptr) {
        SDL_SetAudioStreamGain(g_stream, g_volume);
    }
    SaveSettingsLocked();
}

bool Shuffle() {
    std::lock_guard lock(g_mutex);
    LoadSettingsLocked();
    return g_shuffle;
}

void SetShuffle(bool shuffle) {
    std::lock_guard lock(g_mutex);
    LoadSettingsLocked();
    g_shuffle = shuffle;
    SaveSettingsLocked();
}

bool MuteGameMusic() {
    std::lock_guard lock(g_mutex);
    LoadSettingsLocked();
    return g_muteGameMusic;
}

void SetMuteGameMusic(bool mute) {
    std::lock_guard lock(g_mutex);
    LoadSettingsLocked();
    g_muteGameMusic = mute;
    UpdateGameMusicLocked();
    SaveSettingsLocked();
}

Status GetStatus() {
    std::lock_guard lock(g_mutex);
    Status status;
    status.index = g_index;
    status.loaded = g_loaded;
    status.playing = g_loaded && !g_paused;
    if (g_loaded && g_track.hz > 0) {
        const uint64_t queued =
            g_stream != nullptr ? static_cast<uint64_t>(std::max(0, SDL_GetAudioStreamQueued(g_stream))) / (static_cast<uint64_t>(g_track.channels) * 2u) : 0;
        const uint64_t played = g_track.decodedFrames > queued ? g_track.decodedFrames - queued : 0;
        status.positionSeconds = static_cast<double>(played) / g_track.hz;
        status.durationSeconds = static_cast<double>(g_track.totalFrames) / g_track.hz;
    }
    return status;
}

}  // namespace MusicPlayer
