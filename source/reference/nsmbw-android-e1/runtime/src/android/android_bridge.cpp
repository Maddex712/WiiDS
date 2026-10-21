// android_bridge.cpp - JNI entry points the Android app (com.wither.nsmbw) calls into libmain.so.
#if defined(__ANDROID__)

#include "display_settings.h"
#include "graphics_info.h"
#include "runtime_config.h"
#include "settings_overlay.h"
#include "virtual_remote.h"
#include "thor_state.h"
#include "thor_world_map.h"
#include "thor_reserve.h"

#include "netplay/net_socket.h"
#include "netplay_session.h"
#include "netplay_start.h"
#include "netplay_ui.h"

#include <SDL3/SDL_system.h>
#include <jni.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace AndroidBridge {

// Network play: ask the activity to start the game again in a fresh process (GameActivity
// .restartForSession), then park this thread until Android ends the process.
bool RestartGame() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) {
        return false;
    }
    jclass type = env->GetObjectClass(activity);
    jmethodID restart = type != nullptr ? env->GetMethodID(type, "restartForSession", "()V") : nullptr;
    if (restart == nullptr) {
        env->ExceptionClear();
        return false;
    }
    env->CallVoidMethod(activity, restart);
    env->DeleteLocalRef(activity);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace AndroidBridge

extern "C" {

JNIEXPORT jintArray JNICALL Java_com_wither_nsmbw_GameActivity_nativeReadThorState(JNIEnv* env, jclass)
{
    const auto state = ThorState::Read();
    if (state[0] != 2) return nullptr;
    jintArray result = env->NewIntArray(static_cast<jsize>(state.size()));
    if (result) env->SetIntArrayRegion(result, 0, static_cast<jsize>(state.size()), state.data());
    return result;
}

JNIEXPORT jintArray JNICALL Java_com_wither_nsmbw_GameActivity_nativeReadThorWorldMap(JNIEnv* env, jclass)
{
    const auto words = ThorWorldMap::ReadWords();
    jintArray result = env->NewIntArray(static_cast<jsize>(words.size()));
    if (result) env->SetIntArrayRegion(result, 0, static_cast<jsize>(words.size()), words.data());
    return result;
}

JNIEXPORT jintArray JNICALL Java_com_wither_nsmbw_GameActivity_nativeReadThorReserve(JNIEnv* env, jclass)
{
    const auto words = ThorReserve::ReadWords();
    jintArray result = env->NewIntArray(static_cast<jsize>(words.size()));
    if (result) env->SetIntArrayRegion(result, 0, static_cast<jsize>(words.size()), words.data());
    return result;
}

JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeUseThorReserve(JNIEnv*, jclass)
{
    // The reserve's stage callback is not run on the world map. Gate this UI
    // request with the continuously published scene so a stale canUse flag
    // cannot queue a redemption for the next course.
    const auto state = ThorState::Read();
    if (state[0] == 2 && (state[2] & 2) != 0 && state[4] == 0)
        ThorReserve::RequestUse();
}

// GameActivity.nativeSetTouchState: the on-screen remote (VirtualRemote::TouchButton bits) and the
// phone's motion (shake, tilt -1..1) for player 1. Called from the UI thread on every change.
JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeSetTouchState(
    JNIEnv*, jclass, jint buttons, jboolean shake, jfloat tilt)
{
    VirtualRemote::SetTouchState(static_cast<uint32_t>(buttons), shake == JNI_TRUE, tilt);
}

// GameActivity.nativeApplyDisplaySettings: the Display menu. The aspect (0 original 4:3, 1 16:9,
// 2 fill the screen) and the render scale (480 lines per 1x) apply at the next frame; 4:3 versus a
// widescreen mode only at the next start, since the game reads it at boot.
JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeApplyDisplaySettings(
    JNIEnv*, jclass, jint aspect, jfloat renderScale, jboolean showFps)
{
    // Through the settings overlay, so the game's own settings window shows the same choice. A scale
    // of 0 is "match the screen".
    settings_overlay::SetAspect(aspect < 0 || aspect > 2 ? DisplaySettings::kAspectFill : static_cast<int32_t>(aspect));
    settings_overlay::SetRenderScale(renderScale >= 0.0f ? renderScale : 1.0f);
    settings_overlay::SetShowFps(showFps == JNI_TRUE);
}

// GameActivity.nativeGraphicsInfo: the GPU the game is drawing with and whether the GPU
// compatibility fixes are on, as "<name>\n<1|0>"; empty until the renderer has started.
JNIEXPORT jstring JNICALL Java_com_wither_nsmbw_GameActivity_nativeGraphicsInfo(JNIEnv* env, jclass)
{
    const std::string name = GraphicsInfo::Name();
    if (name.empty()) {
        return env->NewStringUTF("");
    }
    const std::string info = name + "\n" + (GraphicsInfo::CompatActive() ? "1" : "0");
    return env->NewStringUTF(info.c_str());
}

// GameActivity.nativeSetDeviceName: what this phone is called in room lists.
JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeSetDeviceName(JNIEnv* env, jclass, jstring name)
{
    if (name == nullptr) {
        return;
    }
    const char* utf8 = env->GetStringUTFChars(name, nullptr);
    if (utf8 != nullptr) {
        Netplay::SetDeviceName(utf8);
        env->ReleaseStringUTFChars(name, utf8);
    }
}

// GameActivity.nativeSessionActive: whether this run is a LAN session (the menu offers to leave it).
JNIEXPORT jboolean JNICALL Java_com_wither_nsmbw_GameActivity_nativeSessionActive(JNIEnv*, jclass)
{
    return NetplaySession::Active() ? JNI_TRUE : JNI_FALSE;
}

// GameActivity.nativePrepareToLeave: the menu's "Leave LAN game", before the app restarts the game.
JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativePrepareToLeave(JNIEnv*, jclass)
{
    NetplayStart::PrepareToLeave();
}

// GameActivity.nativeTap: a tap on the game's menus, as fractions of the screen (netplay_ui.h).
JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeTap(JNIEnv*, jclass, jfloat x, jfloat y)
{
    NetplayUi::Tap(x, y);
}

// GameActivity.nativeTakeSettings: {render scale, show FPS 1/0, aspect, spinning coins 1/0, frame graph 1/0, frame rate 60/120} when the game's settings
// window changed them since the last call, else null; the app saves them for the next start.
JNIEXPORT jfloatArray JNICALL Java_com_wither_nsmbw_GameActivity_nativeTakeSettings(JNIEnv* env, jclass)
{
    float scale = 1.0f;
    bool showFps = false;
    int aspect = DisplaySettings::kAspectFill;
    bool spinningCoins = true;
    bool frameGraph = false;
    if (!NetplayUi::TakeSettingsChange(scale, showFps, aspect, spinningCoins, frameGraph)) {
        return nullptr;
    }
    jfloatArray result = env->NewFloatArray(6);
    const jfloat values[6] = {scale, showFps ? 1.0f : 0.0f, static_cast<jfloat>(aspect), spinningCoins ? 1.0f : 0.0f,
                              frameGraph ? 1.0f : 0.0f, settings_overlay::HighFrameRate() ? 120.0f : 60.0f};
    env->SetFloatArrayRegion(result, 0, 6, values);
    return result;
}

// GameActivity.nativeTapScreen: whether a menu that taps drive is up (the app hides its remote).
JNIEXPORT jboolean JNICALL Java_com_wither_nsmbw_GameActivity_nativeTapScreen(JNIEnv*, jclass)
{
    return NetplayUi::TapScreenUp() ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"

#endif // __ANDROID__
