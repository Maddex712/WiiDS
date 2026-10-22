#include "ThorSettingsBridge.h"

#include <android/log.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "../input/GamepadInput.h"
#include "../ui/UIOverlay.h"
#include "../ui/SettingsMenuActions.h"
#include "port/port.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyThorMenu", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "GalaxyThorMenu", __VA_ARGS__)

namespace mobile_thor {

namespace {
    std::mutex sQueueMutex;
    std::vector<ThorMenuRequest> sQueue;
    std::atomic<bool> sOpen{false};

    void push(ThorMenuRequest request) {
        std::lock_guard<std::mutex> lock(sQueueMutex);
        if (sQueue.size() < 64) sQueue.push_back(request);
    }

    void JNICALL nativeMenuState(JNIEnv*, jclass, jboolean open) {
        push({open ? THOR_MENU_OPEN : THOR_MENU_CLOSE, 0, 0});
    }

    void JNICALL nativeMenuSet(JNIEnv*, jclass, jint key, jint value) {
        push({THOR_MENU_SET, key, value});
    }

    // The current value of every ThorSetting, in order. Read from the UI thread: the fields are plain
    // ints and bools written only on the native thread while the game is frozen.
    void JNICALL nativeMenuValues(JNIEnv* env, jclass, jintArray out) {
        if (out == nullptr || env->GetArrayLength(out) < THOR_SET_COUNT) return;
        const mobile_ui::Settings& s = mobile_ui::UIOverlay::get().settings();
        jint v[THOR_SET_COUNT];
        v[THOR_SET_RESOLUTION] = s.renderScale;
        v[THOR_SET_FRAME_RATE] = s.batterySaver || s.targetFps <= 30 ? 30 : 60;
        v[THOR_SET_SMOOTHING] = port_is_post_processing_enabled() ? 1 : 0;
        v[THOR_SET_SHOW_FPS] = s.perfHudMode;
        v[THOR_SET_RIGHT_STICK] = s.rightStickMode;
        v[THOR_SET_POINTER_SPEED] = (jint)(s.starStickSensitivity * 100.0f + 0.5f);
        v[THOR_SET_STICK_RESPONSE] = (jint)(s.stickSensitivity * 100.0f + 0.5f);
        v[THOR_SET_INVERT_CAMERA] = s.cameraInverted ? 1 : 0;
        v[THOR_SET_LANGUAGE] = s.languageChoice;
        v[THOR_SET_SKIP_MOVIES] = s.modSkipCutscenes ? 1 : 0;
        v[THOR_SET_INFINITE_LIVES] = s.modInfiniteLives ? 1 : 0;
        v[THOR_SET_INFINITE_HEALTH] = s.modInfiniteHealth ? 1 : 0;
        v[THOR_SET_INFINITE_STAR_BITS] = s.modInfiniteStarBits ? 1 : 0;
        v[THOR_SET_QUICK_SAVE] = 0;
        for (int i = 0; i < mobile_input::BTN_COUNT; i++) v[THOR_SET_BUTTON_BASE + i] = s.buttonMap[i];
        v[THOR_SET_BUTTONS_RESET] = 0;
        env->SetIntArrayRegion(out, 0, THOR_SET_COUNT, v);
    }

    bool attach(ANativeActivity* activity, JNIEnv*& env, bool& attached) {
        env = nullptr;
        attached = false;
        if (activity == nullptr || activity->vm == nullptr) return false;
        jint got = activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (got == JNI_EDETACHED) {
            if (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr) return false;
            attached = true;
        } else if (got != JNI_OK || env == nullptr) {
            return false;
        }
        return true;
    }
}  // namespace

void registerSettingsBridge(ANativeActivity* activity) {
    JNIEnv* env;
    bool attached;
    if (!attach(activity, env, attached)) return;
    jclass clazz = env->GetObjectClass(activity->clazz);
    if (clazz != nullptr) {
        static const JNINativeMethod methods[] = {
            {"nativeMenuState", "(Z)V", (void*)nativeMenuState},
            {"nativeMenuSet", "(II)V", (void*)nativeMenuSet},
            {"nativeMenuValues", "([I)V", (void*)nativeMenuValues},
        };
        for (const JNINativeMethod& method : methods) {
            if (env->RegisterNatives(clazz, &method, 1) == JNI_OK) {
                LOGI("settings menu bridge: %s registered", method.name);
            } else {
                if (env->ExceptionCheck()) env->ExceptionClear();
                LOGW("settings menu bridge: %s not registered", method.name);
            }
        }
        env->DeleteLocalRef(clazz);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) activity->vm->DetachCurrentThread();
}

bool takeMenuRequest(ThorMenuRequest& out) {
    std::lock_guard<std::mutex> lock(sQueueMutex);
    if (sQueue.empty()) return false;
    out = sQueue.front();
    sQueue.erase(sQueue.begin());
    return true;
}

bool settingsMenuOpen() { return sOpen.load(); }
void setSettingsMenuOpen(bool open) { sOpen.store(open); }

namespace {
    bool sQuickSave;  // native main thread only
}  // namespace

bool takeQuickSave() {
    const bool wanted = sQuickSave;
    sQuickSave = false;
    return wanted;
}

void applySetting(int key, int value) {
    mobile_ui::Settings& s = mobile_ui::UIOverlay::get().settings();
    std::string toast;
    if (key >= THOR_SET_BUTTON_BASE && key < THOR_SET_BUTTON_BASE + mobile_input::BTN_COUNT) {
        s.buttonMap[key - THOR_SET_BUTTON_BASE] = std::max(0, std::min(mobile_input::ACT_COUNT - 1, value));
        mobile_input::setButtonMap(s.buttonMap);
        return;
    }
    switch (key) {
    case THOR_SET_RESOLUTION:
        s.renderScale = std::max(0, std::min(3, value));
        break;
    case THOR_SET_FRAME_RATE:
        mobile_ui::SettingsMenuActions::applySliderValue(mobile_ui::ACT_TARGET_FPS, value <= 30 ? 30.0f : 60.0f, s, toast);
        break;
    case THOR_SET_SMOOTHING:
        s.postProcess = value ? 1 : 0;
        port_set_post_processing_enabled(s.postProcess);
        break;
    case THOR_SET_SHOW_FPS:
        s.perfHudMode = std::max(0, std::min(2, value));
        break;
    case THOR_SET_RIGHT_STICK:
        s.rightStickMode = value ? 1 : 0;
        break;
    case THOR_SET_POINTER_SPEED:
        s.starStickSensitivity = std::max(0.5f, std::min(2.5f, value / 100.0f));
        break;
    case THOR_SET_STICK_RESPONSE:
        s.stickSensitivity = std::max(0.5f, std::min(2.0f, value / 100.0f));
        break;
    case THOR_SET_INVERT_CAMERA:
        s.cameraInverted = value != 0;
        port_input_set_camera_inverted(s.cameraInverted ? 1 : 0);
        break;
    case THOR_SET_LANGUAGE:
        mobile_ui::SettingsMenuActions::applyDropdownSelection(mobile_ui::ACT_LANGUAGE, std::max(0, std::min(5, value)), s,
                                                              mobile_ui::UIOverlay::get().saveManager(), toast);
        break;
    case THOR_SET_SKIP_MOVIES:
        s.modSkipCutscenes = value != 0;
        port_set_skip_cutscenes(s.modSkipCutscenes ? 1 : 0);
        break;
    case THOR_SET_INFINITE_LIVES:
        s.modInfiniteLives = value != 0;
        port_set_infinite_lives(s.modInfiniteLives ? 1 : 0);
        break;
    case THOR_SET_INFINITE_HEALTH:
        s.modInfiniteHealth = value != 0;
        port_set_infinite_health(s.modInfiniteHealth ? 1 : 0);
        break;
    case THOR_SET_QUICK_SAVE:
        sQuickSave = true;
        return;
    case THOR_SET_BUTTONS_RESET:
        for (int i = 0; i < mobile_input::BTN_COUNT; i++) s.buttonMap[i] = mobile_input::kDefaultButtonMap[i];
        mobile_input::setButtonMap(s.buttonMap);
        break;
    case THOR_SET_INFINITE_STAR_BITS:
        s.modInfiniteStarBits = value != 0;
        port_set_infinite_starbits(s.modInfiniteStarBits ? 1 : 0);
        break;
    default:
        return;
    }
    mobile_ui::UIOverlay::get().saveSettings();
    LOGI("setting %d = %d", key, value);
}

bool showSettingsMenu(ANativeActivity* activity) {
    JNIEnv* env;
    bool attached;
    if (!attach(activity, env, attached)) return false;
    bool shown = false;
    jclass clazz = env->GetObjectClass(activity->clazz);
    if (clazz != nullptr) {
        jmethodID mid = env->GetStaticMethodID(clazz, "openSettingsMenu", "()Z");
        if (mid != nullptr) {
            shown = env->CallStaticBooleanMethod(clazz, mid) == JNI_TRUE;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) activity->vm->DetachCurrentThread();
    return shown;
}

}  // namespace mobile_thor
