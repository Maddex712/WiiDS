// See ffb.h.
#include "ffb.h"
#include "telemetry.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
// After the standard headers: SDL_endian.h trips over clang's intrinsics otherwise.
#include <SDL.h>

namespace {

FfbSettings g_set;
SDL_Joystick* g_joy;
int g_axis;
float g_lock = 1.0f;
SDL_Haptic* g_hap;
unsigned g_caps;
int g_stop = -1, g_centre = -1, g_damper = -1, g_road = -1, g_rumble = -1;
SDL_HapticEffect g_stop_fx, g_centre_fx, g_damper_fx, g_road_fx, g_rumble_fx;
uint32_t g_start_ms;
uint32_t g_last_ms;

// What the game last said about its vehicle (telemetry::set_listener), and the kicks made
// from its changes; written on the game's thread.
std::mutex g_motion_mutex;
telemetry::Motion g_motion;
float g_kick = 0.0f;       // the road and knocks, -1 to 1, taken by the next update
float g_prev_diff = 0.0f, g_prev_sway = 0.0f;
bool g_prev_known = false;

float g_centre_now = 0.0f;  // the centring spring's coefficient, eased
float g_kick_now = 0.0f;    // the road force, fading after each kick
float g_prev_p = 0.0f;      // the wheel at the last update

Sint16 coeff(float f) { return (Sint16)std::clamp((int)std::lround(f * 32767.0f), -32767, 32767); }
Uint16 sat(float f) { return (Uint16)std::clamp((int)std::lround(f * 65535.0f), 0, 65535); }

void on_motion(const telemetry::Motion& m) {
    std::lock_guard<std::mutex> lk(g_motion_mutex);
    g_motion = m;
    if (!m.active || m.discontinuity) {
        g_prev_known = false;
        return;
    }
    // A bump under one front wheel and not the other turns the steering; a knock shows as
    // a step in the sideways acceleration.
    float k = 0.0f;
    const float diff = m.has_suspension ? m.suspension[0] - m.suspension[1] : 0.0f;
    if (g_prev_known) {
        if (m.has_suspension) k += std::clamp((diff - g_prev_diff) / 0.02f, -1.0f, 1.0f) * g_set.road;
        const float step = m.sway - g_prev_sway;
        if (fabsf(step) > 15.0f) k += std::clamp(step / 40.0f, -1.0f, 1.0f) * g_set.jolt;
    }
    if (!m.has_suspension && m.roughness > 0.0f) {
        const float by_speed = std::min(1.0f, m.speed_kmh / std::max(1.0f, g_set.speed_kmh));
        k += (rand() / (float)RAND_MAX * 2.0f - 1.0f) * m.roughness * by_speed * g_set.road;
    }
    g_prev_diff = diff;
    g_prev_sway = m.sway;
    g_prev_known = true;
    if (fabsf(k) > fabsf(g_kick)) g_kick = k;
}

SDL_HapticEffect spring(Uint16 type) {
    SDL_HapticEffect e;
    SDL_memset(&e, 0, sizeof(e));
    e.type = type;
    e.condition.type = type;
    e.condition.direction.type = SDL_HAPTIC_CARTESIAN;
    e.condition.direction.dir[0] = 1;
    e.condition.length = SDL_HAPTIC_INFINITY;
    return e;
}

int start(SDL_HapticEffect& e, const char* what) {
    const int id = SDL_HapticNewEffect(g_hap, &e);
    if (id < 0 || SDL_HapticRunEffect(g_hap, id, 1) < 0) {
        fprintf(stderr, "force feedback: no %s (%s)\n", what, SDL_GetError());
        if (id >= 0) SDL_HapticDestroyEffect(g_hap, id);
        return -1;
    }
    return id;
}

void close_haptic() {
    if (!g_hap) return;
    SDL_HapticStopAll(g_hap);
    SDL_HapticClose(g_hap);
    g_hap = nullptr;
    g_stop = g_centre = g_damper = g_road = g_rumble = -1;
}

void open_haptic() {
    g_hap = SDL_HapticOpenFromJoystick(g_joy);
    if (!g_hap) {
        fprintf(stderr, "force feedback: \"%s\" has none SDL can drive (%s)\n", SDL_JoystickName(g_joy), SDL_GetError());
        return;
    }
    g_caps = SDL_HapticQuery(g_hap);
    // The wheel's own centring would fight ours.
    if (g_caps & SDL_HAPTIC_AUTOCENTER) SDL_HapticSetAutocenter(g_hap, 0);
    if (g_caps & SDL_HAPTIC_GAIN) SDL_HapticSetGain(g_hap, 100);
    if (g_caps & SDL_HAPTIC_SPRING) {
        g_stop_fx = spring(SDL_HAPTIC_SPRING);
        g_stop = start(g_stop_fx, "stop");
        g_centre_fx = spring(SDL_HAPTIC_SPRING);
        g_centre = start(g_centre_fx, "centring");
    } else {
        fprintf(stderr, "force feedback: \"%s\" has no spring effect, so no stop at full lock and no centring\n",
                SDL_JoystickName(g_joy));
    }
    if (g_caps & SDL_HAPTIC_DAMPER) {
        g_damper_fx = spring(SDL_HAPTIC_DAMPER);
        g_damper = start(g_damper_fx, "damper");
    }
    if (g_caps & SDL_HAPTIC_CONSTANT) {
        SDL_memset(&g_road_fx, 0, sizeof(g_road_fx));
        g_road_fx.type = SDL_HAPTIC_CONSTANT;
        g_road_fx.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        g_road_fx.constant.direction.dir[0] = 1;
        g_road_fx.constant.length = SDL_HAPTIC_INFINITY;
        g_road = start(g_road_fx, "road");
    }
    if (g_caps & SDL_HAPTIC_SINE) {
        SDL_memset(&g_rumble_fx, 0, sizeof(g_rumble_fx));
        g_rumble_fx.type = SDL_HAPTIC_SINE;
        g_rumble_fx.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        g_rumble_fx.periodic.direction.dir[0] = 1;
        g_rumble_fx.periodic.length = SDL_HAPTIC_INFINITY;
        g_rumble_fx.periodic.period = 30;  // ms
        g_rumble = start(g_rumble_fx, "rumble");
    }
    g_start_ms = SDL_GetTicks();
    g_centre_now = 0.0f;
    fprintf(stderr, "force feedback on \"%s\": full lock at %.0f%% of its travel, strength %.2f\n",
            SDL_JoystickName(g_joy), g_lock * 100.0f, g_set.gain);
}

// An update the wheel is sent only when it changes, so that a wheel at rest is not
// flooded with them.
bool differs(const SDL_HapticCondition& a, const SDL_HapticCondition& b) {
    return a.center[0] != b.center[0] || a.deadband[0] != b.deadband[0] || a.right_coeff[0] != b.right_coeff[0] ||
           a.left_coeff[0] != b.left_coeff[0] || a.right_sat[0] != b.right_sat[0] || a.left_sat[0] != b.left_sat[0];
}

void update_condition(int id, SDL_HapticEffect& cur, const SDL_HapticEffect& want) {
    if (id < 0 || !differs(cur.condition, want.condition)) return;
    cur = want;
    SDL_HapticUpdateEffect(g_hap, id, &cur);
}

}  // namespace

void ffb_configure(const FfbSettings& s) {
    g_set = s;
    telemetry::set_listener(s.on ? on_motion : nullptr);
}

void ffb_attach(SDL_Joystick* j, int axis, float lock) {
    if (j == g_joy && axis == g_axis && lock == g_lock) return;
    close_haptic();
    g_joy = j;
    g_axis = axis;
    g_lock = std::clamp(lock, 0.01f, 1.0f);
    if (g_joy && g_set.on) open_haptic();
}

void ffb_update(bool motor) {
    if (!g_hap) return;
    const uint32_t now = SDL_GetTicks();
    const float dt = std::min(0.1f, (now - g_last_ms) / 1000.0f);
    g_last_ms = now;
    // Everything comes in over a second and a half: a wheel left turned past the lock is
    // brought back, not thrown.
    const float ramp = std::min(1.0f, (now - g_start_ms) / 1500.0f);
    const float gain = g_set.gain * ramp;

    telemetry::Motion m;
    float kick;
    {
        std::lock_guard<std::mutex> lk(g_motion_mutex);
        m = g_motion;
        kick = g_kick;
        g_kick = 0.0f;
    }

    // The stop. Inside the lock, a spring with the lock as its dead band: the wheel's own
    // firmware starts it at the lock, however far the wheel turns before the next update.
    // Past it and going in or held there, one side only, its centre moved in so that it
    // pushes back stop_stiffness times harder than a spring can. Coming back out, the
    // spring alone: the stiff one would keep its push until the next update and throw the
    // wheel back at the centre.
    const float p = std::clamp(SDL_JoystickGetAxis(g_joy, g_axis) / 32767.0f, -1.0f, 1.0f);
    const float velocity = dt > 0.0f ? (p - g_prev_p) / dt : 0.0f;  // axis units a second
    g_prev_p = p;
    if (g_stop >= 0) {
        const float over = fabsf(p) - g_lock;
        const bool leaving = (p > 0.0f ? velocity : -velocity) < -0.05f;
        SDL_HapticEffect w = spring(SDL_HAPTIC_SPRING);
        SDL_HapticCondition& c = w.condition;
        c.right_sat[0] = c.left_sat[0] = sat(g_set.stop * ramp);  // not scaled by gain: a stop is a stop
        if (over <= 0.0f || g_lock >= 1.0f || leaving) {
            c.deadband[0] = sat(g_lock);
            c.right_coeff[0] = c.left_coeff[0] = coeff(1.0f);
        } else {
            const float side = p > 0.0f ? 1.0f : -1.0f;
            const float centre = std::clamp(p - side * over * g_set.stop_stiffness, -1.0f, 1.0f);
            c.center[0] = coeff(centre);
            if (side > 0.0f) c.right_coeff[0] = coeff(1.0f);
            else c.left_coeff[0] = coeff(1.0f);
        }
        update_condition(g_stop, g_stop_fx, w);
    }

    // The damper, much heavier past the lock. Updates come only so often, and between them
    // the stop's spring keeps the push it had: leaving the stop, that would throw the wheel
    // back towards the centre. A damper only ever resists movement, so it takes the energy
    // out of that without being able to add any.
    if (g_damper >= 0) {
        const bool past = g_lock < 1.0f && fabsf(p) > g_lock;
        SDL_HapticEffect w = spring(SDL_HAPTIC_DAMPER);
        // Past the lock, like the stop, not scaled by gain.
        const float k = past ? std::max(g_set.damper * gain, g_set.stop_damper * ramp) : g_set.damper * gain;
        w.condition.right_coeff[0] = w.condition.left_coeff[0] = coeff(k);
        w.condition.right_sat[0] = w.condition.left_sat[0] = sat(past ? std::max(gain, g_set.stop * ramp) : gain);
        update_condition(g_damper, g_damper_fx, w);
    }

    // Centring, eased so that the start of a race or a pause does not jerk it.
    if (g_centre >= 0) {
        float want = g_set.centre_rest;
        if (m.active) {
            const float by_speed = std::min(1.0f, m.speed_kmh / std::max(1.0f, g_set.speed_kmh));
            // The slip angle, sideways against forwards: a corner taken hard slips a few
            // degrees; a slide is from 5 degrees to all of it at 30.
            const float slip = atan2f(fabsf(m.sway_velocity), std::max(1.0f, m.speed_kmh / 3.6f)) * 57.29578f;
            const float slide = std::clamp((slip - 5.0f) / 25.0f, 0.0f, 1.0f);
            want = g_set.centre_rest + (g_set.centre - g_set.centre_rest) * by_speed + g_set.load * fabsf(m.sway) / 9.81f;
            want *= 1.0f - g_set.slide * slide;
        }
        g_centre_now += (want - g_centre_now) * std::min(1.0f, dt / 0.2f);
        SDL_HapticEffect w = spring(SDL_HAPTIC_SPRING);
        // Coarse steps: a change too small to feel is not worth an update.
        const float k = std::round(std::clamp(g_centre_now * gain, 0.0f, 1.0f) * 200.0f) / 200.0f;
        w.condition.right_coeff[0] = w.condition.left_coeff[0] = coeff(k);
        w.condition.right_sat[0] = w.condition.left_sat[0] = sat(gain);
        update_condition(g_centre, g_centre_fx, w);
    }

    if (g_road >= 0) {
        // Each kick fades over a few tens of milliseconds, however often this runs.
        g_kick_now *= expf(-dt / 0.04f);
        if (fabsf(kick) > fabsf(g_kick_now)) g_kick_now = kick;
        if (fabsf(g_kick_now) < 0.01f) g_kick_now = 0.0f;  // and then stops being sent
        const Sint16 level = coeff(std::clamp(g_kick_now, -1.0f, 1.0f) * gain);
        if (level != g_road_fx.constant.level) {
            g_road_fx.constant.level = level;
            SDL_HapticUpdateEffect(g_hap, g_road, &g_road_fx);
        }
    }

    if (g_rumble >= 0) {
        const Sint16 mag = motor ? coeff(g_set.rumble * gain) : 0;
        if (mag != g_rumble_fx.periodic.magnitude) {
            g_rumble_fx.periodic.magnitude = mag;
            SDL_HapticUpdateEffect(g_hap, g_rumble, &g_rumble_fx);
        }
    }
}

void ffb_shutdown() {
    close_haptic();
    g_joy = nullptr;
}
