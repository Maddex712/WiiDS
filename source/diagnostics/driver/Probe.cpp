// Read-only diagnostics, packaged only into the separate signed instrumentation.
#include <jni.h>
#include <dlfcn.h>
#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <aurora/gfx.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
static_assert(sizeof(PADButtonMapping) == 8 && offsetof(PADButtonMapping, padButton) == 4);
static_assert(sizeof(PADAxisMapping) == 16 && offsetof(PADAxisMapping, nativeButton) == 8 &&
              offsetof(PADAxisMapping, padAxis) == 12);
struct Json {
    char data[65536]{};
    size_t used = 0;
    bool truncated = false;

    void add(const char* format, ...) {
        if (truncated) return;
        va_list args;
        va_start(args, format);
        const int count = vsnprintf(data + used, sizeof(data) - used, format, args);
        va_end(args);
        if (count < 0 || static_cast<size_t>(count) >= sizeof(data) - used) {
            truncated = true;
            return;
        }
        used += static_cast<size_t>(count);
    }
    void string(const char* value) {
        if (!value) { add("null"); return; }
        add("\"");
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
            // ASCII escapes also keep NewStringUTF independent of device-name UTF-8 details.
            if (*p == '\\' || *p == '"') add("\\%c", *p);
            else if (*p < 0x20 || *p >= 0x7f) add("\\u%04x", *p);
            else add("%c", *p);
        }
        add("\"");
    }
    jstring finish(JNIEnv* env) {
        return env->NewStringUTF(truncated ? "{\"ok\":false,\"error\":\"probe JSON capacity exceeded\"}" : data);
    }
};

void raceConfigProbe(Json& json, void* library) {
    // Scalars only: never call guest code, PADRead, or any runtime initializer.
    const auto symbol = [library](const char* name) {
        void* result = library ? dlsym(library, name) : nullptr;
        return result ? result : dlsym(RTLD_DEFAULT, name);
    };
    using Read32 = uint32_t (*)(uint32_t);
    using Read8 = uint8_t (*)(uint32_t);
    using Contains = bool (*)(uint32_t, size_t);
    const auto read32 = reinterpret_cast<Read32>(symbol("_ZN6Memory6Read32Ej"));
    const auto read8 = reinterpret_cast<Read8>(symbol("_ZN6Memory5Read8Ej"));
    const auto contains = reinterpret_cast<Contains>(symbol("_ZN6Memory8ContainsEjm"));
    json.add(",\"race_config\":{\"available\":%s", read32 && contains ? "true" : "false");
    if (!read32 || !contains) {
        json.add(",\"error\":\"Memory::Read32/Contains export missing\"}");
        return;
    }
    const auto readable = [contains](uint32_t address, size_t length) {
        const bool cachedRam = (address >= 0x80000000u && address < 0x82000000u) ||
                               (address >= 0x90000000u && address < 0x98000000u);
        return cachedRam && (address & 3u) == 0 && contains(address, length);
    };
    constexpr uint32_t singleton = 0x809BD728u, raceSingleton = 0x809BD730u;
    if (!readable(singleton, 4) || !readable(raceSingleton, 4)) {
        json.add(",\"valid\":false,\"error\":\"PAL singleton memory unavailable\"}");
        return;
    }
    const uint32_t data = read32(singleton), race = read32(raceSingleton);
    json.add(",\"address\":%u", data);
    if (!readable(data, 0xB90)) {
        json.add(",\"valid\":false,\"error\":\"RaceConfig pointer invalid\"}");
        return;
    }
    const auto byte = [read32, read8](uint32_t address) -> uint8_t {
        if (read8) return read8(address);
        // Read8 is inlined in this runtime; guest scalar values are big endian.
        return static_cast<uint8_t>(read32(address & ~3u) >> ((3u - (address & 3u)) * 8u));
    };
    const bool haveRace = readable(race, 0x2C);
    const uint32_t frameBefore = haveRace ? read32(race + 0x20) : 0;
    const unsigned count = byte(data + 0x24), hudCount = byte(data + 0x25), localCount = byte(data + 0x26);
    // PAL Scenario starts at +0x20; Settings starts at +0xB68. The camera
    // getter 8052ED18 reads scenario+0xB54, and Player::GetPlayerType
    // 8052ED20 reads player+0x10. Player construction confirms byte +5/+6.
    const uint32_t course = read32(data + 0xB68), engine = read32(data + 0xB6C);
    const uint32_t gameMode = read32(data + 0xB70), cameraMode = read32(data + 0xB74);
    const bool valid = count <= 12 && hudCount <= 4 && localCount <= 4 && gameMode <= 12 && cameraMode <= 12;
    json.add(",\"valid\":%s,\"course_slot\":%u,\"engine_class\":%u,\"game_mode\":%u,\"camera_mode\":%u,"
             "\"player_count\":%u,\"hud_count\":%u,\"local_player_count\":%u,\"hud_player_ids\":[",
             valid ? "true" : "false", course, engine, gameMode, cameraMode, count, hudCount, localCount);
    for (unsigned i = 0; i < 4; ++i) {
        if (i) json.add(",");
        json.add("%d", static_cast<int>(static_cast<int8_t>(byte(data + 0xB84 + i))));
    }
    json.add("],\"players\":[");
    for (unsigned i = 0; i < count && i < 12; ++i) {
        if (i) json.add(",");
        const uint32_t player = data + 0x28 + i * 0xF0;
        json.add("{\"id\":%u,\"type\":%u,\"local_number\":%d,\"input_index\":%d,\"controller_id\":%d,"
                 "\"vehicle\":%u,\"character\":%u}",
                 i, read32(player + 0x10), static_cast<int>(static_cast<int8_t>(byte(player + 5))),
                 static_cast<int>(static_cast<int8_t>(byte(player + 6))),
                 static_cast<int32_t>(read32(player + 0xD0)), read32(player + 8), read32(player + 0xC));
    }
    json.add("],\"race_manager_available\":%s,\"stage\":%u,\"frame_before\":%u,\"frame_after\":%u,\"pointer_stable\":%s,"
             "\"atomic_snapshot\":false}", haveRace ? "true" : "false", haveRace ? read32(race + 0x28) : 0,
             frameBefore, haveRace ? read32(race + 0x20) : 0, read32(singleton) == data ? "true" : "false");
}
}

namespace {
void graphicsProbe(Json& json, void* library) {
    const auto symbol = [library](const char* name) {
        void* result = library ? dlsym(library, name) : nullptr;
        return result ? result : dlsym(RTLD_DEFAULT, name);
    };
    const auto timingGetter = reinterpret_cast<decltype(&aurora_get_present_timing)>(symbol("aurora_get_present_timing"));
    const auto interpolationGetter = reinterpret_cast<decltype(&aurora_get_frame_interpolation_diagnostics)>(
        symbol("aurora_get_frame_interpolation_diagnostics"));
    json.add(",\"graphics\":{\"available\":%s", timingGetter && interpolationGetter ? "true" : "false");
    if (timingGetter && interpolationGetter) {
        // The timing getter locks its history; interpolation diagnostics are
        // atomic counters. Neither pumps frames nor changes the selected target.
        AuroraPresentTiming timing{};
        AuroraFrameInterpolationDiagnostics interpolation{};
        timingGetter(&timing);
        interpolationGetter(&interpolation);
        json.add(",\"total_presents\":%llu,\"samples\":%u,\"present_fps\":%.3f,\"effective_fps\":%.3f,"
                 "\"average_ms\":%.3f,\"p95_ms\":%.3f,\"jitter_ms\":%.3f,"
                 "\"target_fps\":%u,\"target_samples\":%u,\"active_samples\":%u,\"candidates\":%u,"
                 "\"matchable\":%u,\"matches\":%u,\"eligible\":%u,\"replay_safe\":%u,"
                 "\"frames_sealed\":%llu,\"frames_low_match\":%llu,\"frames_replay_unsafe\":%llu,"
                 "\"slot_reductions\":%llu,\"late_seal_drops\":%llu,\"atomic_snapshot\":false",
                 static_cast<unsigned long long>(timing.totalPresentCount), timing.sampleCount,
                 timing.framesPerSecond, timing.effectiveFramesPerSecond, timing.averageFrameTimeMs,
                 timing.p95FrameTimeMs, timing.jitterMs, interpolation.targetFps, interpolation.targetSamples,
                 interpolation.activeSamples, interpolation.candidates, interpolation.matchable,
                 interpolation.matches, interpolation.eligible, interpolation.replaySafe,
                 static_cast<unsigned long long>(interpolation.framesSealed),
                 static_cast<unsigned long long>(interpolation.framesLowMatch),
                 static_cast<unsigned long long>(interpolation.framesReplayUnsafe),
                 static_cast<unsigned long long>(interpolation.slotReductions),
                 static_cast<unsigned long long>(interpolation.lateSealDrops));
    }
    json.add("}");
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_thor_mkw_driver_Driver_nativeProbe(JNIEnv* env, jclass) {
    Json json;
    // NOLOAD is deliberate: this probe must never initialize a second runtime.
    const char* runtime = "libRetroRewind.so";
    void* library = dlopen(runtime, RTLD_NOW | RTLD_NOLOAD);
    if (!library) {
        runtime = "libWiiCompiled.so";
        library = dlopen(runtime, RTLD_NOW | RTLD_NOLOAD);
    }
    const auto symbol = [library](const char* name) {
        void* result = library ? dlsym(library, name) : nullptr;
        return result ? result : dlsym(RTLD_DEFAULT, name);
    };
    json.add("{\"runtime\":"); json.string(runtime);
    json.add(",\"library_handle\":%s,\"missing\":[", library ? "true" : "false");
    bool missing = false;
#define LOAD(name) \
    auto name = reinterpret_cast<decltype(&::name)>(symbol(#name)); \
    if (!name) { if (missing) json.add(","); json.string(#name); missing = true; }
    LOAD(SDL_GetGamepads);
    LOAD(SDL_GetGamepadFromID);
    LOAD(SDL_GetGamepadNameForID);
    LOAD(SDL_GetGamepadPlayerIndex);
    LOAD(SDL_GetGamepadButton);
    LOAD(SDL_GetGamepadJoystick);
    LOAD(SDL_GetGamepadMapping);
    LOAD(SDL_GetJoysticks);
    LOAD(SDL_GetJoystickFromID);
    LOAD(SDL_GetJoystickNameForID);
    LOAD(SDL_GetJoystickID);
    LOAD(SDL_GetJoystickVendor);
    LOAD(SDL_GetJoystickProduct);
    LOAD(SDL_GetNumJoystickButtons);
    LOAD(SDL_GetJoystickButton);
    LOAD(SDL_GetNumJoystickAxes);
    LOAD(SDL_GetJoystickAxis);
    LOAD(SDL_GetKeyboardState);
    LOAD(SDL_GetKeyboardFocus);
    LOAD(SDL_GetWindowID);
    LOAD(SDL_GetWindowFlags);
    LOAD(SDL_GetHint);
    LOAD(SDL_GetError);
    LOAD(SDL_LockJoysticks);
    LOAD(SDL_UnlockJoysticks);
    LOAD(SDL_free);
    LOAD(PADGetIndexForPort);
    LOAD(PADGetName);
    LOAD(PADGetSDLGamepadForIndex);
    LOAD(PADGetButtonMappings);
    LOAD(PADGetAltButtonMappings);
    LOAD(PADGetAxisMappings);
    LOAD(PADGetDeadZones);
#undef LOAD
    json.add("]");
    if (missing) {
        json.add(",\"ok\":false,\"loader_error\":");
        json.string(dlerror());
        json.add("}");
        if (library) dlclose(library);
        return json.finish(env);
    }

    // SDL getters use this recursive lock themselves. Holding it across the
    // snapshot prevents a concurrent device close invalidating returned handles.
    SDL_LockJoysticks();
    int padCount = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&padCount);
    json.add(",\"ok\":true,\"gamepad_count\":%d,\"pads\":[", padCount);
    for (int i = 0; pads && i < padCount && i < 32; ++i) {
        if (i) json.add(",");
        SDL_Gamepad* pad = SDL_GetGamepadFromID(pads[i]);
        json.add("{\"sdl_id\":%u,\"name\":", pads[i]);
        json.string(SDL_GetGamepadNameForID(pads[i]));
        json.add(",\"open\":%s", pad ? "true" : "false");
        if (pad) {
            SDL_Joystick* joystick = SDL_GetGamepadJoystick(pad);
            json.add(",\"player\":%d,\"south\":%s,\"east\":%s,\"start\":%s,\"joystick_id\":%u,\"mapping\":",
                     SDL_GetGamepadPlayerIndex(pad),
                     SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH) ? "true" : "false",
                     SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST) ? "true" : "false",
                     SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START) ? "true" : "false",
                     joystick ? SDL_GetJoystickID(joystick) : 0);
            char* mapping = SDL_GetGamepadMapping(pad);
            json.string(mapping);
            SDL_free(mapping);
        }
        json.add("}");
    }
    SDL_free(pads);

    int joystickCount = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&joystickCount);
    json.add("],\"joystick_count\":%d,\"joysticks\":[", joystickCount);
    for (int i = 0; joysticks && i < joystickCount && i < 32; ++i) {
        if (i) json.add(",");
        SDL_Joystick* joystick = SDL_GetJoystickFromID(joysticks[i]);
        json.add("{\"sdl_id\":%u,\"name\":", joysticks[i]);
        json.string(SDL_GetJoystickNameForID(joysticks[i]));
        json.add(",\"open\":%s", joystick ? "true" : "false");
        if (joystick) {
            const int buttons = SDL_GetNumJoystickButtons(joystick);
            const int axes = SDL_GetNumJoystickAxes(joystick);
            json.add(",\"vendor\":%u,\"product\":%u,\"button_count\":%d,\"raw_buttons\":[",
                     SDL_GetJoystickVendor(joystick), SDL_GetJoystickProduct(joystick), buttons);
            for (int b = 0; b < buttons && b < 64; ++b) {
                json.add("%s%d", b ? "," : "", SDL_GetJoystickButton(joystick, b) ? 1 : 0);
            }
            json.add("],\"axes\":[");
            for (int a = 0; a < axes && a < 16; ++a) {
                json.add("%s%d", a ? "," : "", SDL_GetJoystickAxis(joystick, a));
            }
            json.add("]");
        }
        json.add("}");
    }
    SDL_free(joysticks);
    const int controllerIndex = PADGetIndexForPort(0);
    SDL_Gamepad* controller = controllerIndex < 0 ? nullptr : PADGetSDLGamepadForIndex(controllerIndex);
    SDL_Joystick* controllerJoystick = controller ? SDL_GetGamepadJoystick(controller) : nullptr;
    json.add("],\"pad_port0\":{\"controller_index\":%d,\"name\":", controllerIndex);
    json.string(PADGetName(0));
    json.add(",\"sdl_id\":%u,\"buttons\":[", controllerJoystick ? SDL_GetJoystickID(controllerJoystick) : 0);
    unsigned int heldMask = 0;
    // These getters can load a saved mapping if it has not yet been loaded by
    // the running game. They never consume PADRead's blocked/held suppression.
    u32 count = 0;
    const PADButtonMapping* buttons = PADGetButtonMappings(0, &count);
    for (u32 i = 0; buttons && i < count && i < PAD_BUTTON_COUNT; ++i) {
        const bool held = controller && buttons[i].nativeButton < SDL_GAMEPAD_BUTTON_COUNT &&
            SDL_GetGamepadButton(controller, static_cast<SDL_GamepadButton>(buttons[i].nativeButton));
        if (held) heldMask |= buttons[i].padButton;
        json.add("%s{\"native\":%u,\"pad\":%u,\"held\":%s}", i ? "," : "",
                 buttons[i].nativeButton, buttons[i].padButton, held ? "true" : "false");
    }
    json.add("],\"alt_buttons\":[");
    const PADButtonMapping* altButtons = PADGetAltButtonMappings(0, &count);
    for (u32 i = 0; altButtons && i < count && i < PAD_BUTTON_COUNT; ++i) {
        const bool held = controller && altButtons[i].nativeButton < SDL_GAMEPAD_BUTTON_COUNT &&
            SDL_GetGamepadButton(controller, static_cast<SDL_GamepadButton>(altButtons[i].nativeButton));
        if (held) heldMask |= altButtons[i].padButton;
        json.add("%s{\"native\":%u,\"pad\":%u,\"held\":%s}", i ? "," : "",
                 altButtons[i].nativeButton, altButtons[i].padButton, held ? "true" : "false");
    }
    json.add("],\"mapped_button_mask_before_suppression\":%u,\"axes\":[", heldMask);
    const PADAxisMapping* axes = PADGetAxisMappings(0, &count);
    for (u32 i = 0; axes && i < count && i < PAD_AXIS_COUNT; ++i) {
        json.add("%s{\"native_axis\":%d,\"sign\":%d,\"native_button\":%d,\"pad_axis\":%u}",
                 i ? "," : "", axes[i].nativeAxis.nativeAxis, axes[i].nativeAxis.sign,
                 axes[i].nativeButton, axes[i].padAxis);
    }
    json.add("],\"deadzones\":");
    const PADDeadZones* deadzones = PADGetDeadZones(0);
    if (deadzones) {
        json.add("{\"emulate_triggers\":%s,\"use_deadzones\":%s,\"stick\":%u,\"substick\":%u,\"left_trigger\":%u,\"right_trigger\":%u}",
                 deadzones->emulateTriggers ? "true" : "false", deadzones->useDeadzones ? "true" : "false",
                 deadzones->stickDeadZone, deadzones->substickDeadZone,
                 deadzones->leftTriggerActivationZone, deadzones->rightTriggerActivationZone);
    } else json.add("null");
    json.add("}");
    SDL_UnlockJoysticks();
    SDL_Window* focus = SDL_GetKeyboardFocus();
    json.add(",\"sdl_keyboard_focus\":%s,\"focus_window_id\":%u,\"focus_window_flags\":%llu,\"joystick_background_hint\":",
             focus ? "true" : "false", focus ? SDL_GetWindowID(focus) : 0,
             static_cast<unsigned long long>(focus ? SDL_GetWindowFlags(focus) : 0));
    json.string(SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS));
    int keyCount = 0;
    const bool* keys = SDL_GetKeyboardState(&keyCount);
    json.add(",\"keyboard_return\":%s,\"keyboard_space\":%s,\"sdl_error\":",
             keys && keyCount > SDL_SCANCODE_RETURN && keys[SDL_SCANCODE_RETURN] ? "true" : "false",
             keys && keyCount > SDL_SCANCODE_SPACE && keys[SDL_SCANCODE_SPACE] ? "true" : "false");
    json.string(SDL_GetError());
    raceConfigProbe(json, library);
    graphicsProbe(json, library);
    json.add("}");
    if (library) dlclose(library);
    return json.finish(env);
}

// Startup isolation: stage 0 reads only the existing SDL player's buttons;
// stage 1 invokes exactly the lazy PAD mapping getters used by the full probe.
// Neither stage sends input, opens controllers, pumps events, or reads guest code.
extern "C" JNIEXPORT jstring JNICALL
Java_dev_thor_mkw_driver_Driver_nativeProbeStage(JNIEnv* env, jclass, jint stage) {
    Json json;
    if (stage != 0 && stage != 1) {
        json.add("{\"ok\":false,\"error\":\"stage must be 0 (SDL) or 1 (PAD mappings)\"}");
        return json.finish(env);
    }
    void* library = dlopen("libRetroRewind.so", RTLD_NOW | RTLD_NOLOAD);
    if (!library) library = dlopen("libWiiCompiled.so", RTLD_NOW | RTLD_NOLOAD);
    const auto symbol = [library](const char* name) { return library ? dlsym(library, name) : nullptr; };
    const auto lock = reinterpret_cast<decltype(&SDL_LockJoysticks)>(symbol("SDL_LockJoysticks"));
    const auto unlock = reinterpret_cast<decltype(&SDL_UnlockJoysticks)>(symbol("SDL_UnlockJoysticks"));
    const auto fromPlayer = reinterpret_cast<decltype(&SDL_GetGamepadFromPlayerIndex)>(symbol("SDL_GetGamepadFromPlayerIndex"));
    const auto button = reinterpret_cast<decltype(&SDL_GetGamepadButton)>(symbol("SDL_GetGamepadButton"));
    const auto primary = reinterpret_cast<decltype(&PADGetButtonMappings)>(symbol("PADGetButtonMappings"));
    const auto alternate = reinterpret_cast<decltype(&PADGetAltButtonMappings)>(symbol("PADGetAltButtonMappings"));
    const auto axes = reinterpret_cast<decltype(&PADGetAxisMappings)>(symbol("PADGetAxisMappings"));
    if (!lock || !unlock || !fromPlayer || !button || !primary || !alternate || !axes) {
        json.add("{\"ok\":false,\"error\":\"startup isolation dependency missing\"}");
    } else {
        lock();
        SDL_Gamepad* pad = fromPlayer(0);
        json.add("{\"ok\":true,\"stage\":%d,\"pad_open\":%s,\"south\":%s,\"east\":%s", stage,
                 pad ? "true" : "false", pad && button(pad, SDL_GAMEPAD_BUTTON_SOUTH) ? "true" : "false",
                 pad && button(pad, SDL_GAMEPAD_BUTTON_EAST) ? "true" : "false");
        if (stage == 1) {
            uint32_t primaryCount = 0, alternateCount = 0, axisCount = 0;
            const PADButtonMapping* mappings = primary(0, &primaryCount);
            alternate(0, &alternateCount);
            axes(0, &axisCount);
            json.add(",\"primary_count\":%u,\"alternate_count\":%u,\"axis_count\":%u,\"primary\":[",
                     primaryCount, alternateCount, axisCount);
            for (uint32_t i = 0; mappings && i < primaryCount && i < PAD_BUTTON_COUNT; ++i) {
                json.add("%s{\"native\":%u,\"pad\":%u}", i ? "," : "", mappings[i].nativeButton, mappings[i].padButton);
            }
            json.add("]");
        }
        json.add("}");
        unlock();
    }
    if (library) dlclose(library);
    return json.finish(env);
}
