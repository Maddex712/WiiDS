// See telemetry.h.
#include "telemetry.h"
#include "runtime.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t kNoSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
static const sock_t kNoSocket = -1;
#endif

namespace telemetry {
namespace {

sock_t g_sock = kNoSocket;
sockaddr_in g_dest{};
std::map<std::string, float> g_keys;
std::map<std::string, float> g_defaults;   // the game's own (set_default)
bool g_log = false;

float key(const char* k, float fallback) {
    auto it = g_keys.find(k);
    if (it != g_keys.end()) return it->second;
    it = g_defaults.find(k);
    return it == g_defaults.end() ? fallback : it->second;
}

void read_file() {
    FILE* f = fopen("telemetry.txt", "rb");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char k[64];
        double v;
        if (line[0] == '#' || sscanf(line, "%63s %lf", k, &v) != 2) continue;
        g_keys[k] = (float)v;
    }
    fclose(f);
}

// SimRacingStudio's custom-game packet, version 102: Python's struct format
// "3sI50s50s50sfffifffffffffffIIII" in its native layout, which on every platform this runs
// on puts a byte of padding after the 3-byte tag and two after the strings. 236 bytes,
// little-endian. The suspension and terrain fields describe wheels, which a hull has not
// got; they are left at zero.
constexpr size_t kSrsSize = 236;
void put_str(uint8_t* p, size_t at, size_t n, const char* s) { strncpy((char*)p + at, s, n - 1); }
void put_u32(uint8_t* p, size_t at, uint32_t v) { memcpy(p + at, &v, 4); }
void put_f32(uint8_t* p, size_t at, float v) { memcpy(p + at, &v, 4); }

void send_srs(const Motion& m) {
    uint8_t p[kSrsSize] = {};
    memcpy(p, "api", 3);
    put_u32(p, 4, 102);
    put_str(p, 8, 50, GCN_GAME_TITLE);
    put_str(p, 58, 50, "");
    put_str(p, 108, 50, "");
    put_f32(p, 160, m.speed_kmh);
    put_f32(p, 164, m.rpm);
    put_f32(p, 168, m.max_rpm);
    put_u32(p, 172, (uint32_t)m.gear);
    put_f32(p, 176, m.pitch);
    put_f32(p, 180, m.roll);
    put_f32(p, 184, m.yaw);
    put_f32(p, 188, m.sway_velocity);  // lateral velocity
    put_f32(p, 192, m.sway);           // lateral acceleration
    put_f32(p, 196, m.heave);          // vertical acceleration
    put_f32(p, 200, m.surge);          // longitudinal acceleration
    for (int w = 0; w < 4; w++) put_f32(p, 204 + 4 * w, m.suspension[w]);   // FL, FR, RL, RR
    sendto(g_sock, (const char*)p, (int)kSrsSize, 0, (const sockaddr*)&g_dest, sizeof(g_dest));
}

}  // namespace

bool configure(const char* spec) {
    read_file();
    std::string host = "127.0.0.1";
    int port = 33001;
    if (spec && *spec) {
        std::string s(spec);
        const size_t colon = s.rfind(':');
        if (colon == std::string::npos) {
            host = s;
        } else {
            host = s.substr(0, colon);
            port = atoi(s.c_str() + colon + 1);
        }
    }
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    g_dest.sin_family = AF_INET;
    g_dest.sin_port = htons((uint16_t)port);
    if (port <= 0 || port > 65535 || inet_pton(AF_INET, host.c_str(), &g_dest.sin_addr) != 1) {
        fprintf(stderr, "telemetry: '%s' is not an IPv4 host:port\n", spec);
        return false;
    }
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock == kNoSocket) return false;
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(g_sock, FIONBIO, &nb);
#endif
    g_log = key("log", 0.0f) != 0.0f || getenv("GCN_TELEMETRY_LOG");
    fprintf(stderr, "telemetry: SimRacingStudio API packets to %s:%d\n", host.c_str(), port);
    return true;
}

static void (*g_listener)(const Motion&);
void set_listener(void (*fn)(const Motion&)) { g_listener = fn; }

bool enabled() { return g_sock != kNoSocket || g_listener; }

float get(const char* k, float fallback) { return key(k, fallback); }

void set_default(const char* k, float value) { g_defaults[k] = value; }

// What goes to the platform is built in three stages, each of which on its own would stop a
// class of jerk, so that no single mistake upstream reaches the rider:
//
// 1. Rest and back. A race starting or ending, a pause, the vehicle lost: the motion eases
//    out to rest over `fade_s`, holding its last value on the way, and eases back in. The
//    ease is a smoothstep, so the platform leaves rest and arrives there with no velocity of
//    its own; a linear fade starts it moving at full speed in an instant.
// 2. Discontinuities. A respawn puts the vehicle somewhere else in one frame, so whatever
//    was being sent is crossfaded, over `fade_s` and eased the same way, into the new values
//    rather than stepped to them.
// 3. Limits, off unless asked for. A key sets a range or a rate of change on the motion
//    channels: `max_angle` degrees for pitch, roll and yaw, `angle_rate` degrees a second
//    for pitch and roll and `yaw_rate` for yaw, `max_accel` m/s^2 and `accel_rate` m/s^2 a
//    second for the accelerations; 0, the default, is no limit. They used to be on (25
//    degrees, 120 and 60 a second, 15 m/s^2 and 300 a second), and were taken off when a
//    rider found them unnecessary: a platform has its own travel and its own speed, and the
//    jerks a limit would catch are the discontinuities stages 1 and 2 already take out. At
//    60 degrees and 60 m/s^2 a second, before that, they had been smoothing as well, and the
//    motion was tame: a wave's chop held the accelerations at the limit four frames in five,
//    a collision reached the platform as a few m/s^2 and sometimes the wrong way, and a dive
//    off a ramp (the hull pitching at some 300 degrees a second) as a slow lean. Smoothing
//    belongs upstream, where the game knows what is noise. The speed and the slide keep
//    their bounds; they are not motion.
//
// There used to be a fourth, ahead of the limits: a yaw washout, for a game that sent its
// vehicle's heading. The heading was unwrapped and what went out was how far it had turned
// from where it was `yaw_washout_s` ago, so every turn turned the platform and let it drift
// back. Game after game had to be moved off it onto the slip: a platform turning with
// every corner was jarring, while the slip, which a rider feels through the seat, holds
// still on a clean line. A heading is the wrong input, not one to wash out, so the option
// is gone and `yaw` is only ever the platform's own angle.
//
// Between the gains and the limits, a road texture (4), eased in and out with the rest of the
// motion and scaled by `texture_gain`. A platform that sat dead still on a straight felt like
// no road at all; one that hummed with band-limited noise felt like noise, all one note.
// - From the suspension, when the game has it: each wheel's compression less its own
//   running mean (over `suspension_hp_s`), so the springs' movement and not their load. Heave
//   is their mean times `suspension_heave` (m/s^2 a metre); pitch the front pair against the
//   rear over `wheelbase_m`, roll the left against the right over `track_m`, as angles times
//   `suspension_pitch` and `suspension_roll`. A bump comes through as the wheels met it.
// - Made up, from `roughness`, for a game without: bumps, at `texture_bumps` a second at
//   `texture_speed_kmh` and over, each of a random size and split between heave, pitch and
//   roll, ringing in a damped spring at `texture_hz`; under them a finer grain of noise
//   (`texture_grain` of the bumps' size). `texture_heave`, `texture_pitch` and `texture_roll`
//   size it at roughness 1, in m/s^2 and degrees.
namespace {

enum Ch { kSpeed, kPitch, kRoll, kYaw, kSurge, kSway, kHeave, kSlide, kChannels };

void to_array(const Motion& m, float* v) {
    v[kSpeed] = m.speed_kmh; v[kPitch] = m.pitch; v[kRoll] = m.roll; v[kYaw] = m.yaw;
    v[kSurge] = m.surge; v[kSway] = m.sway; v[kHeave] = m.heave; v[kSlide] = m.sway_velocity;
}

float smoothstep(float t) {
    t = fminf(1.0f, fmaxf(0.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

void publish(const Motion& in) {
    if (g_listener) g_listener(in);
    if (g_sock == kNoSocket) return;
    static auto t_prev = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float dt = fminf(0.1f, std::chrono::duration<float>(now - t_prev).count());
    t_prev = now;
    const float fade = key("fade_s", 1.0f);
    const float fade_step = fade > 0.0f ? dt / fade : 1.0f;

    static float held[kChannels];        // the last active input: what rest fades away from
    if (in.active) to_array(in, held);

    // Rest and back (1).
    static float rest_t = 0.0f;   // 0 at rest, 1 following the input
    rest_t = in.active ? fminf(1.0f, rest_t + fade_step) : fmaxf(0.0f, rest_t - fade_step);
    const float level = smoothstep(rest_t);
    float want[kChannels];
    for (int c = 0; c < kChannels; c++) want[c] = held[c] * level;

    // Discontinuities (2): crossfade from what was being sent.
    static float out[kChannels];   // what was last sent
    static float cross_from[kChannels];
    static float cross_t = 1.0f;
    if (in.active && in.discontinuity) {
        for (int c = 0; c < kChannels; c++) cross_from[c] = out[c];
        cross_t = 0.0f;
    }
    if (cross_t < 1.0f) {
        cross_t = fminf(1.0f, cross_t + fade_step);
        const float s = smoothstep(cross_t);
        for (int c = 0; c < kChannels; c++) want[c] = cross_from[c] + (want[c] - cross_from[c]) * s;
    }

    // Gains, then limits (3).
    want[kPitch] *= key("pitch_gain", 1.0f);
    want[kRoll] *= key("roll_gain", 1.0f);
    want[kYaw] *= key("yaw_gain", 1.0f);
    want[kSurge] *= key("surge_gain", 1.0f);
    want[kSway] *= key("sway_gain", 1.0f);
    want[kHeave] *= key("heave_gain", 1.0f);

    // Road texture (4).
    {
        static uint32_t seed = 0x2545F491u;
        auto noise = [] {
            seed = seed * 1664525u + 1013904223u;
            return (float)(seed >> 8) * (2.0f / 16777216.0f) - 1.0f;   // uniform in [-1, 1)
        };
        constexpr float kDegrees = 57.2957795f;
        const float gain = level * key("texture_gain", 1.0f);
        float tex[3] = {0.0f, 0.0f, 0.0f};   // heave m/s^2, pitch and roll degrees

        // From the suspension.
        static float sus_mean[4];
        static bool have_sus = false;
        if (in.active && in.has_suspension) {
            if (!have_sus || in.discontinuity)
                for (int w = 0; w < 4; w++) sus_mean[w] = in.suspension[w];
            have_sus = true;
            const float hp = key("suspension_hp_s", 0.25f);
            const float a = hp > 0.0f ? 1.0f - expf(-dt / hp) : 1.0f;
            float dev[4];
            for (int w = 0; w < 4; w++) {
                sus_mean[w] += (in.suspension[w] - sus_mean[w]) * a;
                dev[w] = in.suspension[w] - sus_mean[w];
            }
            const float front = (dev[0] + dev[1]) * 0.5f, rear = (dev[2] + dev[3]) * 0.5f;
            const float left = (dev[0] + dev[2]) * 0.5f, right = (dev[1] + dev[3]) * 0.5f;
            tex[0] += (front + rear) * 0.5f * key("suspension_heave", 40.0f);
            // A front wheel pushed up lifts the nose; a left one lowers the right side.
            tex[1] += atan2f(front - rear, key("wheelbase_m", 1.0f)) * kDegrees * key("suspension_pitch", 0.6f);
            tex[2] += atan2f(left - right, key("track_m", 1.2f)) * kDegrees * key("suspension_roll", 0.6f);
        } else {
            have_sus = false;
        }

        // Made up, from the roughness: bumps ringing in a damped spring, over a grain.
        static float ring[3], ring_v[3], grain[3];
        const float rough = in.active ? fminf(1.0f, fmaxf(0.0f, in.roughness)) : 0.0f;
        const float at_speed = fminf(1.0f, fabsf(held[kSpeed]) / fmaxf(1.0f, key("texture_speed_kmh", 60.0f)));
        const float size[3] = {key("texture_heave", 1.5f), key("texture_pitch", 0.3f), key("texture_roll", 0.5f)};
        if (rough > 0.0f && at_speed > 0.0f) {
            // A bump: a kick to the spring's velocity, its size exponential about 1, split
            // at random between the channels.
            if ((noise() * 0.5f + 0.5f) < key("texture_bumps", 4.0f) * at_speed * dt) {
                const float big = -logf(fmaxf(1e-4f, noise() * 0.5f + 0.5f));
                for (int c = 0; c < 3; c++) ring_v[c] += big * noise() * 2.0f;
            }
        }
        // The spring: x'' = -w^2 x - 2 zeta w x', stepped semi-implicitly; normalised so a
        // kick of 1 peaks at about 1.
        const float w = 6.2831853f * key("texture_hz", 6.0f);
        const float zeta = 0.3f;
        for (int c = 0; c < 3; c++) {
            ring_v[c] += (-w * w * ring[c] - 2.0f * zeta * w * ring_v[c]) * dt;
            ring[c] += ring_v[c] * dt;
            if (!(rough > 0.0f)) ring[c] = ring_v[c] = 0.0f;
        }
        const float a = 1.0f - expf(-dt * 6.2831853f * 12.0f);
        for (float& g : grain) g += (noise() - g) * a;
        const float amp = rough * at_speed;
        // Skipped with no time passed (the first packet, two in one clock tick): `a` is then
        // 0 and the grain's normalisation infinite, and 0 times it is NaN. While the limits
        // were on, the rate limit's fmaxf hid that NaN by taking its other operand; with them
        // off it reached the platform on the first packet of every run.
        if (amp > 0.0f && a > 0.0f) {
            const float grain_size = key("texture_grain", 0.25f) / sqrtf(a / (2.0f - a) / 3.0f);   // unit RMS
            for (int c = 0; c < 3; c++) tex[c] += amp * size[c] * (ring[c] * w + grain[c] * grain_size);
        }

        want[kHeave] += tex[0] * gain;
        want[kPitch] += tex[1] * gain;
        want[kRoll] += tex[2] * gain;
    }

    // Limits (3): 0 is none.
    const float max_angle = key("max_angle", 0.0f), angle_rate = key("angle_rate", 0.0f);
    const float yaw_rate = key("yaw_rate", 0.0f);
    const float max_accel = key("max_accel", 0.0f), accel_rate = key("accel_rate", 0.0f);
    const float range[kChannels] = {1000.0f, max_angle, max_angle, max_angle, max_accel, max_accel, max_accel, 10.0f};
    const float rate[kChannels] = {200.0f, angle_rate, angle_rate, yaw_rate, accel_rate, accel_rate, accel_rate, 20.0f};
    for (int c = 0; c < kChannels; c++) {
        // A value that is not a number holds the last one sent: whatever produced it, upstream
        // or in a game's own arithmetic, a platform must not be handed it.
        if (!std::isfinite(want[c])) want[c] = out[c];
        const float v = range[c] > 0.0f ? fminf(range[c], fmaxf(-range[c], want[c])) : want[c];
        const float step = rate[c] * dt;
        out[c] = rate[c] > 0.0f ? fminf(out[c] + step, fmaxf(out[c] - step, v)) : v;
    }

    Motion m;
    m.active = in.active;
    m.speed_kmh = out[kSpeed]; m.pitch = out[kPitch]; m.roll = out[kRoll]; m.yaw = out[kYaw];
    m.surge = out[kSurge]; m.sway = out[kSway]; m.heave = out[kHeave]; m.sway_velocity = out[kSlide];
    // The engine eases to rest with the motion: the revs wind down to nothing and back up with
    // the level, from the last active ones, rather than cutting out when a race ends or pauses
    // and coming back at full revs. A shaker plays a step in the revs as a thump.
    static float rpm_held, max_rpm_held;
    static int gear_held = 1;
    if (in.active) {
        rpm_held = in.rpm;
        max_rpm_held = in.max_rpm;
        gear_held = in.gear;
        for (int w = 0; w < 4; w++) m.suspension[w] = in.suspension[w];
    }
    m.rpm = rpm_held * level;
    m.max_rpm = max_rpm_held;
    m.gear = gear_held;
    send_srs(m);

    if (g_log) {
        static int n;
        static bool was_active;
        const bool change = in.active != was_active || in.discontinuity;
        was_active = in.active;
        if (change)
            fprintf(stderr, "[telemetry] %s\n", in.discontinuity ? "discontinuity: crossfading" : in.active ? "active: easing in" : "rest: easing out");
        if (n++ % 30 == 0)
            fprintf(stderr, "[telemetry] %s %5.1f km/h pitch %+5.1f roll %+5.1f yaw %+6.1f | surge %+6.2f sway %+6.2f heave %+6.2f m/s2 slide %+5.2f m/s | %4.0f rpm\n",
                    in.active ? "active" : "rest  ", m.speed_kmh, m.pitch, m.roll, m.yaw, m.surge, m.sway, m.heave, m.sway_velocity, m.rpm);
    }
}

}  // namespace telemetry
