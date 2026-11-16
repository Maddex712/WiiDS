#include <port_video.h>

#include <port_menu.h>
#include <settings.h>

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#else
#include <port_win_time.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

extern "C" {
void PortInput_LatchHeld(void);
}

namespace {

namespace es = engine::settings;

constexpr double kGameHz = 60.0;
constexpr int kWindowHeight = 960;

enum class Pacing { Original, Auto, LowLatency };

struct Choices {
  bool widescreen = false;
  float aspect = 0.f;
  const char* shapeName = "";
  Pacing pacing = Pacing::Original;
  int32_t resolutionScale = 1;
  bool fullscreen = false;
  bool borderless = false;
};

std::atomic<float> sWideScale{1.f};

Choices read_settings() {
  const es::Registry& r = es::Global();
  Choices c;
  c.widescreen = r.Bool("video.widescreen").Get();
  const int32_t shape = r.Enum("video.widescreen_shape").Get();
  if (shape == r.ChoiceIndex("video.widescreen_shape", "16_10")) {
    c.aspect = 16.f / 10.f;
    c.shapeName = "16:10";
  } else if (shape == r.ChoiceIndex("video.widescreen_shape", "21_9")) {
    c.aspect = 21.f / 9.f;
    c.shapeName = "21:9";
  } else if (shape == r.ChoiceIndex("video.widescreen_shape", "window")) {
    c.aspect = -1.f;
    c.shapeName = "the window's shape";
  } else {
    c.aspect = 16.f / 9.f;
    c.shapeName = "16:9";
  }
  const int32_t pacing = r.Enum("video.frame_pacing").Get();
  if (pacing == r.ChoiceIndex("video.frame_pacing", "auto")) {
    c.pacing = Pacing::Auto;
  } else if (pacing == r.ChoiceIndex("video.frame_pacing", "low_latency")) {
    c.pacing = Pacing::LowLatency;
  } else if (r.Enum("video.frame_rate").Get() == r.ChoiceIndex("video.frame_rate", "60")) {
    c.pacing = Pacing::Auto;
  }
  c.resolutionScale = r.Int("video.resolution_scale").Get();
  c.fullscreen = r.Bool("video.fullscreen").Get();
  c.borderless = r.Bool("video.borderless_window").Get();
  return c;
}

Choices sChoices;
bool sWindowResolution = false;
AuroraSampler sSampler = SAMPLER_BILINEAR;
bool sVsyncAllowed = true;
bool sVsyncOn = true;
std::atomic<bool> sFastForward{false};
float sMonitorHz = 0.f;

float monitor_hz(SDL_Window* window) {
  const SDL_DisplayID display = window != nullptr ? SDL_GetDisplayForWindow(window) : 0;
  const SDL_DisplayMode* mode = display != 0 ? SDL_GetCurrentDisplayMode(display) : nullptr;
  if (mode == nullptr) {
    return 0.f;
  }
  if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
    return static_cast<float>(static_cast<double>(mode->refresh_rate_numerator) / mode->refresh_rate_denominator);
  }
  return mode->refresh_rate;
}

bool wants_vsync(Pacing pacing, float hz, const char** why) {
  switch (pacing) {
  case Pacing::Original:
    *why = "original: every picture waits for the monitor's refresh (vsync)";
    return true;
  case Pacing::LowLatency:
    *why = "lowest delay: no vsync, the newest picture is shown at each refresh";
    return false;
  case Pacing::Auto:
  default:
    if (hz <= 0.f) {
      *why = "matched to the monitor: its refresh rate is unknown, so vsync as in Original";
      return true;
    }
    if (hz >= kGameHz * 0.999) {
      *why = "matched to the monitor: at least 59.94 Hz, so vsync (as in Original)";
      return true;
    }
    *why = "matched to the monitor: slower than the game's 59.94 Hz, so no vsync (pictures it can't show are "
           "skipped instead of slowing the game down)";
    return false;
  }
}

void describe_monitor(SDL_Window* window, const char* when) {
  const SDL_DisplayID display = window != nullptr ? SDL_GetDisplayForWindow(window) : 0;
  const SDL_DisplayMode* mode = display != 0 ? SDL_GetCurrentDisplayMode(display) : nullptr;
  const float hz = monitor_hz(window);
  const double perPicture = hz > 0.f ? hz / kGameHz : 0.0;
  std::fprintf(stderr, "[video] %s: monitor \"%s\" %dx%d at %.3f Hz = %.3f refreshes per game field (59.94 Hz)%s\n",
               when, display != 0 ? SDL_GetDisplayName(display) : "?", mode != nullptr ? mode->w : 0,
               mode != nullptr ? mode->h : 0, static_cast<double>(hz), perPicture,
               perPicture > 0.0 && std::fabs(perPicture - std::round(perPicture)) > 0.01
                   ? " (not a whole number: pictures stay on screen for unequal numbers of refreshes, unless the "
                     "monitor has variable refresh (G-Sync/FreeSync) on)"
                   : "");
}

void apply_pacing(SDL_Window* window, const char* when) {
  sMonitorHz = monitor_hz(window);
  const char* why = "";
  const bool vsync = sVsyncAllowed && wants_vsync(sChoices.pacing, sMonitorHz, &why);
  if (!sVsyncAllowed) {
    why = "no vsync: the game clock runs faster than real time (XD_TURBO)";
  }
  if (vsync != sVsyncOn && !sFastForward.load()) {
    aurora_enable_vsync(vsync);
    sVsyncOn = vsync;
  }
  std::fprintf(stderr, "[video] %s: frame pacing %s\n", when, why);
}

float resolution_scale() {
  if (sWindowResolution) return 0.f;
  // Persist 1080p as the unused choice 7; existing integer scale values stay unchanged.
  return sChoices.resolutionScale == 7 ? 2.25f : static_cast<float>(std::max<int32_t>(0, sChoices.resolutionScale));
}

void apply_resolution(SDL_Window* window, const char* when, bool always) {
  const float scale = resolution_scale();
  int w = 0, h = 0;
  if (window == nullptr || !SDL_GetWindowSizeInPixels(window, &w, &h) || w <= 0 || h <= 0) {
    return;
  }
  double aspect = 4.0 / 3.0;
  if (sChoices.widescreen) {
    aspect = sChoices.aspect > 0.f ? sChoices.aspect : std::max(aspect, static_cast<double>(w) / h);
  }
  sWideScale.store(static_cast<float>(aspect * 0.75), std::memory_order_relaxed);
  const double shownHeight = std::min(static_cast<double>(h), w / aspect);
  const double drawnHeight = scale > 0 ? 480.0 * scale : shownHeight;
  const AuroraSampler sampler = drawnHeight > shownHeight * 1.01 ? SAMPLER_AREA : SAMPLER_BILINEAR;
  if (sampler != sSampler) {
    aurora_set_resampler(sampler);
    sSampler = sampler;
  } else if (!always) {
    return;
  }
  if (scale > 0) {
    std::fprintf(stderr, "[video] %s: resolution %s: drawn %dx%d, shown %.0f lines high in a %dx%d window (%s)\n",
                 when, scale == 1 ? "native (as the GameCube)" : (std::to_string(scale) + "x").c_str(),
                 static_cast<int>(std::lround(drawnHeight * aspect)), static_cast<int>(drawnHeight), shownHeight, w, h,
                 sampler == SAMPLER_AREA                 ? "scaled down: area average"
                 : std::fabs(drawnHeight - shownHeight) < 1.0 ? "same size"
                                                             : "scaled up: bilinear");
  } else {
    std::fprintf(stderr, "[video] %s: resolution: the window's size (%dx%d window)\n", when, w, h);
  }
}

bool sIsFullscreen = false;
bool sIsBorderless = false;
SDL_Rect sNormal{};
bool sHaveNormal = false;
void (*sSaveSettings)(void) = nullptr;

bool env_is(const char* name, const char* value) {
  const char* v = std::getenv(name);
  return v != nullptr && std::strcmp(v, value) == 0;
}

void log_window(SDL_Window* window, const char* when) {
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  std::fprintf(stderr, "[video] %s: %s, %dx%d (frame pacing as before: %s)\n", when,
               sIsFullscreen   ? "full screen (desktop: the monitor keeps its resolution and refresh rate)"
               : sIsBorderless ? "borderless window over its whole monitor"
                               : "a normal window",
               w, h, sVsyncOn ? "vsync" : "no vsync");
}

void set_borderless(SDL_Window* window, bool on) {
  if (on == sIsBorderless) return;
  if (on) {
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) SDL_RestoreWindow(window);
    sHaveNormal =
        SDL_GetWindowPosition(window, &sNormal.x, &sNormal.y) && SDL_GetWindowSize(window, &sNormal.w, &sNormal.h);
    SDL_Rect bounds{};
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (display == 0 || !SDL_GetDisplayBounds(display, &bounds)) {
      std::fprintf(stderr, "[video] borderless window: the monitor's size is unknown (%s)\n", SDL_GetError());
      return;
    }
    if (env_is("XD_WINDOW_OFFSCREEN", "1")) bounds.x = bounds.y = -20000;
    SDL_SetWindowBordered(window, false);
    SDL_SetWindowResizable(window, false);
    SDL_SetWindowPosition(window, bounds.x, bounds.y);
    SDL_SetWindowSize(window, bounds.w, bounds.h);
  } else {
    SDL_SetWindowBordered(window, true);
    SDL_SetWindowResizable(window, true);
    if (sHaveNormal) {
      SDL_SetWindowSize(window, sNormal.w, sNormal.h);
      SDL_SetWindowPosition(window, sNormal.x, sNormal.y);
    }
  }
  sIsBorderless = on;
  SDL_SyncWindow(window);
}

void apply_window_mode(SDL_Window* window, const char* when) {
  if (window == nullptr) return;
  const bool wantFull = sChoices.fullscreen, wantBorderless = sChoices.borderless;
  if (wantFull == sIsFullscreen && (sIsFullscreen || wantBorderless == sIsBorderless)) return;
  if (sIsFullscreen && !wantFull) {
    if (!SDL_SetWindowFullscreen(window, false)) {
      std::fprintf(stderr, "[video] couldn't leave full screen: %s\n", SDL_GetError());
    }
    SDL_SyncWindow(window);
    sIsFullscreen = false;
  }
  if (!sIsFullscreen) set_borderless(window, wantBorderless);
  if (wantFull && !sIsFullscreen) {
    SDL_SetWindowFullscreenMode(window, nullptr);
    if (SDL_SetWindowFullscreen(window, true)) {
      SDL_SyncWindow(window);
      sIsFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    }
    if (!sIsFullscreen) std::fprintf(stderr, "[video] couldn't go full screen: %s\n", SDL_GetError());
  }
  log_window(window, when);
  std::fflush(stderr);
}

void set_bool(const char* id, bool on) {
  es::Registry& r = es::Global();
  const size_t i = r.Find(id);
  if (i == es::kNoSetting) return;
  es::Value v = r.ValueOf(i);
  if ((v.i != 0) == on) return;
  v.i = on ? 1 : 0;
  if (r.Set(i, v) && sSaveSettings != nullptr) sSaveSettings();
}

void toggle_fullscreen() {
  PortInput_LatchHeld();
  PortMenu_LatchHeld();
  const bool on = !es::Global().Bool("video.fullscreen").Get();
  std::fprintf(stderr, "[video] Alt+Enter: full screen %s\n", on ? "on" : "off");
  set_bool("video.fullscreen", on);
  sChoices.fullscreen = es::Global().Bool("video.fullscreen").Get();
  apply_window_mode(aurora_get_window(), "Alt+Enter");
}

struct PresentStats {
  std::mutex mutex;
  double everySeconds = 0.0;
  int64_t qpcFrequency = 1;
  int64_t windowStart = 0;
  int64_t lastPresent = 0;
  int64_t lastRefresh = -1;
  int64_t firstVBlank = 0;
  uint32_t presents = 0;
  std::array<uint32_t, 41> intervalMs{};
  std::array<uint32_t, 9> refreshesHeld{};
  std::array<uint32_t, 10> phase{};
  double periodSum = 0.0;
  uint32_t periodCount = 0;
};
PresentStats sStats;

void print_stats(int64_t now) {
  PresentStats& s = sStats;
  const double seconds = static_cast<double>(now - s.windowStart) / static_cast<double>(s.qpcFrequency);
  const double refreshHz = s.periodCount > 0 ? static_cast<double>(s.qpcFrequency) / (s.periodSum / s.periodCount) : 0.0;
  std::string intervals, held, phase;
  for (size_t i = 0; i < s.intervalMs.size(); ++i) {
    if (s.intervalMs[i] != 0) {
      intervals += " " + std::to_string(i) + (i + 1 == s.intervalMs.size() ? "+" : "") + "ms:" +
                   std::to_string(s.intervalMs[i]);
    }
  }
  for (size_t i = 0; i < s.refreshesHeld.size(); ++i) {
    if (s.refreshesHeld[i] != 0) {
      held += " " + std::to_string(i) + (i + 1 == s.refreshesHeld.size() ? "+" : "") + ":" +
              std::to_string(s.refreshesHeld[i]);
    }
  }
  for (size_t i = 0; i < s.phase.size(); ++i) {
    phase += " " + std::to_string(s.phase[i]);
  }
  std::fprintf(stderr,
               "[present] %.1f s: %u presents (%.3f a second); composition %.3f Hz, vsync %s\n"
               "[present]   time between presents:%s\n"
               "[present]   refreshes each picture stayed (0 = never shown):%s\n"
               "[present]   when in the refresh the present came (tenths of it, from the vertical blank):%s\n",
               seconds, s.presents, seconds > 0 ? s.presents / seconds : 0.0, refreshHz, sVsyncOn ? "on" : "off",
               intervals.c_str(), held.c_str(), phase.c_str());
  std::fflush(stderr);
  s.windowStart = now;
  s.presents = 0;
  s.intervalMs.fill(0);
  s.refreshesHeld.fill(0);
  s.phase.fill(0);
  s.periodSum = 0.0;
  s.periodCount = 0;
}

void present_hook() {
  LARGE_INTEGER qpc;
  QueryPerformanceCounter(&qpc);
  const int64_t now = qpc.QuadPart;
#ifdef _WIN32
  DWM_TIMING_INFO timing{};
  timing.cbSize = sizeof timing;
  const bool haveTiming = SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)) && timing.qpcRefreshPeriod > 0;
#else
  struct {
    uint64_t qpcRefreshPeriod = 0, qpcVBlank = 0;
  } timing;
  const bool haveTiming = false;
#endif

  std::lock_guard lock{sStats.mutex};
  PresentStats& s = sStats;
  if (s.windowStart == 0) {
    s.windowStart = now;
  }
  if (s.lastPresent != 0) {
    const double ms = static_cast<double>(now - s.lastPresent) * 1000.0 / static_cast<double>(s.qpcFrequency);
    s.intervalMs[std::min<size_t>(static_cast<size_t>(std::max(0.0, ms)), s.intervalMs.size() - 1)]++;
  }
  s.lastPresent = now;
  s.presents++;
  if (haveTiming) {
    const int64_t period = static_cast<int64_t>(timing.qpcRefreshPeriod);
    const int64_t vblank = static_cast<int64_t>(timing.qpcVBlank);
    if (s.firstVBlank == 0) {
      s.firstVBlank = vblank;
    }
    const int64_t since = now - vblank;
    const int64_t steps = since >= 0 ? since / period : -((-since + period - 1) / period);
    const int64_t refresh =
        std::llround(static_cast<double>(vblank - s.firstVBlank) / static_cast<double>(period)) + steps;
    const int64_t into = since - steps * period;
    s.phase[std::min<size_t>(static_cast<size_t>(into * 10 / period), s.phase.size() - 1)]++;
    if (s.lastRefresh >= 0) {
      const int64_t held = refresh - s.lastRefresh;
      s.refreshesHeld[static_cast<size_t>(std::clamp<int64_t>(held, 0, s.refreshesHeld.size() - 1))]++;
    }
    s.lastRefresh = refresh;
    s.periodSum += static_cast<double>(period);
    s.periodCount++;
  }
  if (static_cast<double>(now - s.windowStart) >= s.everySeconds * static_cast<double>(s.qpcFrequency)) {
    print_stats(now);
  }
}

struct Pacer {
  bool on = false;
  int64_t frequency = 1;
  double period = 0.0;
  int64_t origin = 0;
  double phase = 0.0;
  std::array<double, 120> lateness{};
  size_t count = 0;
};
Pacer sPacer;
bool sStatsOn = false;

double wrap_half(double x, double period) {
  x = std::fmod(x, period);
  if (x > period * 0.5) {
    x -= period;
  } else if (x <= -period * 0.5) {
    x += period;
  }
  return x;
}

void wait_until(int64_t when) {
  for (;;) {
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    const int64_t left = when - qpc.QuadPart;
    if (left <= 0) {
      return;
    }
    const double ms = static_cast<double>(left) * 1000.0 / static_cast<double>(sPacer.frequency);
    if (ms > 2.0) {
      Sleep(static_cast<DWORD>(ms - 1.5));
    } else {
      YieldProcessor();
    }
  }
}

void before_present_hook() {
  if (!sPacer.on || sFastForward.load(std::memory_order_relaxed)) {
    return;
  }
  LARGE_INTEGER qpc;
  QueryPerformanceCounter(&qpc);
  const int64_t now = qpc.QuadPart;
  Pacer& p = sPacer;
  if (p.origin == 0) {
    p.origin = now;
    p.phase = 0.004 * static_cast<double>(p.frequency);
    return;
  }
  const double late = wrap_half(static_cast<double>(now - p.origin) - p.phase, p.period);
  p.lateness[p.count++ % p.lateness.size()] = late;
  if (p.count % p.lateness.size() == 0) {
    std::array<double, 120> sorted = p.lateness;
    std::sort(sorted.begin(), sorted.end());
    const double q95 = sorted[sorted.size() * 95 / 100];
    p.phase = std::fmod(p.phase + q95 + 0.001 * static_cast<double>(p.frequency) + p.period, p.period);
  }
  if (late < 0.0) {
    wait_until(now + static_cast<int64_t>(-late));
  }
}

void after_present_hook() {
  if (sStatsOn) {
    present_hook();
  }
}

}

extern "C" {

void PortVideo_Configure(AuroraConfig* config) {
  sChoices = read_settings();
  sVsyncAllowed = config->vsync;
  sVsyncOn = config->vsync;
  if (sChoices.widescreen && config->windowWidth == 0 && config->windowHeight == 0) {
    const float aspect = sChoices.aspect > 0.f ? sChoices.aspect : 16.f / 9.f;
    config->windowWidth = static_cast<uint32_t>(std::lround(kWindowHeight * aspect));
    config->windowHeight = kWindowHeight;
  }
}

void PortVideo_UseWindowResolution(void) { sWindowResolution = true; }

float PortVideo_ResolutionScale(void) { return static_cast<float>(resolution_scale()); }

float PortVideo_WideScale(void) { return sWideScale.load(std::memory_order_relaxed); }

void PortVideo_ApplySettings(void) {
  const Choices before = sChoices;
  const Pacing pacing = sChoices.pacing;
  sChoices = read_settings();
  sChoices.pacing = pacing;
  SDL_Window* window = aurora_get_window();
  if (sChoices.resolutionScale != before.resolutionScale) {
    VISetFrameBufferScale(static_cast<float>(resolution_scale()));
    std::fprintf(stderr, "[video] resolution now %g (0 = the window's size)\n", resolution_scale());
  }
  if (sChoices.widescreen != before.widescreen || sChoices.aspect != before.aspect) {
    AuroraSetWidescreen(sChoices.widescreen ? sChoices.aspect : 0.f);
    std::fprintf(stderr, "[video] widescreen now %s\n", sChoices.widescreen ? sChoices.shapeName : "off");
  }
  apply_window_mode(window, "settings");
  apply_resolution(window, "settings", false);
}

void PortVideo_SetSettingsSaver(void (*save)(void)) { sSaveSettings = save; }

void PortVideo_Start(void) {
  SDL_Window* window = aurora_get_window();
  AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);
  VISetFrameBufferScale(static_cast<float>(resolution_scale()));
  if (sChoices.widescreen) {
    AuroraSetWidescreen(sChoices.aspect);
    if (window != nullptr) {
      SDL_Rect usable{};
      int w = 0, h = 0;
      if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable) && SDL_GetWindowSize(window, &w, &h) &&
          w > 0 && h > 0) {
        const double fit = std::min({1.0, usable.w * 0.95 / w, (usable.h - 64) * 0.95 / h});
        if (fit < 1.0) {
          SDL_SetWindowSize(window, static_cast<int>(w * fit), static_cast<int>(h * fit));
        }
        SDL_GetWindowSize(window, &w, &h);
      }
      std::fprintf(stderr, "[video] widescreen %s: window %dx%d; 3D sees more at the sides, the game's 2D stays 4:3 "
                           "in the middle\n",
                   sChoices.shapeName, w, h);
    }
  }
  describe_monitor(window, "start");
  apply_pacing(window, "start");
  if (sChoices.fullscreen || sChoices.borderless) apply_window_mode(window, "start");
  apply_resolution(window, "start", true);

  const char* stats = std::getenv("XD_PRESENT_STATS");
  if (stats != nullptr && stats[0] != '\0' && std::atof(stats) > 0.0) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    sStats.qpcFrequency = frequency.QuadPart;
    sStats.everySeconds = std::atof(stats);
    sStatsOn = true;
    std::fprintf(stderr, "[video] XD_PRESENT_STATS: present timing every %g s\n", sStats.everySeconds);
  }
  const char* det = std::getenv("XD_DETERMINISTIC");
  if (sChoices.pacing == Pacing::Auto && sVsyncAllowed && !(det != nullptr && det[0] == '1')) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    sPacer.frequency = frequency.QuadPart;
    sPacer.period = static_cast<double>(frequency.QuadPart) / kGameHz;
    sPacer.on = true;
    std::fprintf(stderr, "[video] frame pacing: pictures handed to the window on an even 59.94 Hz rhythm\n");
  }
  if (sPacer.on || sStatsOn) {
    aurora_set_present_hooks(before_present_hook, after_present_hook);
  }
  std::fflush(stderr);
}

void PortVideo_SetFastForward(bool on) {
  if (sFastForward.exchange(on) == on) {
    return;
  }
  if (on) {
    if (sVsyncOn) {
      aurora_enable_vsync(false);
      sVsyncOn = false;
    }
    return;
  }
  const char* why = "";
  const bool vsync = sVsyncAllowed && wants_vsync(sChoices.pacing, sMonitorHz, &why);
  if (vsync != sVsyncOn) {
    aurora_enable_vsync(vsync);
    sVsyncOn = vsync;
  }
}

void PortVideo_HandleEvent(const void* sdlEvent) {
  const auto* event = static_cast<const SDL_Event*>(sdlEvent);
  if (event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
    SDL_Window* window = aurora_get_window();
    describe_monitor(window, "window moved");
    apply_pacing(window, "window moved");
    std::fflush(stderr);
  } else if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
    apply_resolution(aurora_get_window(), "window resized", false);
    std::fflush(stderr);
  } else if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
             (event->key.scancode == SDL_SCANCODE_RETURN || event->key.scancode == SDL_SCANCODE_KP_ENTER) &&
             (event->key.mod & SDL_KMOD_ALT) != 0 && (event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) == 0) {
    toggle_fullscreen();
  } else if (event->type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN && sIsFullscreen &&
             (SDL_GetWindowFlags(aurora_get_window()) & SDL_WINDOW_FULLSCREEN) == 0) {
    sIsFullscreen = false;
    set_bool("video.fullscreen", false);
    sChoices.fullscreen = false;
    log_window(aurora_get_window(), "full screen ended");
    std::fflush(stderr);
  }
}

}
