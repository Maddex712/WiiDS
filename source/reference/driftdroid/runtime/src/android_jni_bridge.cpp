// JNI entry points for the Android app. Only compiled into the Android product target - see
// runtime/CMakeLists.txt. nativeRealRuntimeCheck is a standalone P1-era smoke test (loads the
// real translated engine, resolves paths, runs the arm64 CPU baseline check). The real game
// entry point is SDL_main below, invoked by SDLActivity's own SDLMain thread (MainActivity.kt
// now extends SDLActivity) - not called directly from here.
#if defined(__ANDROID__)

#include <jni.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <SDL3/SDL_system.h>

#include "android_jni_dispatch.h"
#include "android_touch_overlay_bridge.h"
#include "hle/storage/wii_disc_extractor.h"
#include "music_attenuation.h"
#include "thermal_quality.h"
#include "runtime_config.h"
#include "runtime_product.h"
#include "settings_overlay.h"

bool g_androidTouchControlsVisibleCache = true;
bool g_androidDoubleTapAutoHoldCache = false;

namespace {

// Shared by both native->Java callbacks below. Posted through AndroidJniDispatch rather than
// called inline - both call sites can run from inside the guest fiber's translated execution
// (settings_overlay::Draw() for the settings checkbox; SDL's gamepad-connect event handling isn't
// guaranteed not to for the other), and a JNI call made from a fiber's swapped native stack
// corrupts ART's CheckJNI bookkeeping - see android_jni_dispatch.h.
void CallVoidMethodOnActivity(const char* methodName, bool value) {
    AndroidJniDispatch::Post([methodName, value] {
        JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (env == nullptr || activity == nullptr) {
            return;
        }
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(activityClass, methodName, "(Z)V");
        if (method != nullptr) {
            env->CallVoidMethod(activity, method, value ? JNI_TRUE : JNI_FALSE);
        }
        env->DeleteLocalRef(activityClass);
    });
}

void CallVoidMethodOnActivity(const char* methodName) {
    AndroidJniDispatch::Post([methodName] {
        JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (env == nullptr || activity == nullptr) {
            return;
        }
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(activityClass, methodName, "()V");
        if (method != nullptr) {
            env->CallVoidMethod(activity, method);
        }
        env->DeleteLocalRef(activityClass);
    });
}

}  // namespace

// settings_overlay.cpp's "Hide the settings button" checkbox.
extern "C" void AndroidSetSettingsButtonHidden(bool hidden) {
    CallVoidMethodOnActivity("onNativeSetSettingsButtonHidden", hidden);
}

// Called from aurora-main/lib/window.cpp's SDL_EVENT_GAMEPAD_ADDED/REMOVED handling - the touch
// overlay auto-hides while a real controller is connected (TouchControlsOverlay.kt combines this
// with the user's own manual on/off preference).
extern "C" void AndroidNotifyGamepadConnectionChanged(bool connected) {
    CallVoidMethodOnActivity("onNativeGamepadConnectionChanged", connected);
}

// Called from the ImGui "Touch controls" settings checkbox (settings_overlay.cpp).
extern "C" void AndroidSetTouchOverlayVisible(bool visible) {
    g_androidTouchControlsVisibleCache = visible;
    CallVoidMethodOnActivity("onNativeSetTouchOverlayVisible", visible);
}

// Called from the ImGui "Double-tap A to auto-hold acceleration" settings checkbox
// (settings_overlay.cpp).
extern "C" void AndroidSetDoubleTapAutoHold(bool enabled) {
    g_androidDoubleTapAutoHoldCache = enabled;
    CallVoidMethodOnActivity("onNativeSetDoubleTapAutoHold", enabled);
}

// Called from SetTopBarVisible (settings_overlay.cpp) whenever the settings sidebar's own
// visibility changes.
extern "C" void AndroidNotifySettingsVisibilityChanged(bool visible) {
    CallVoidMethodOnActivity("onNativeSettingsVisibilityChanged", visible);
}

// Called from the ImGui settings sidebar's "Edit Touch Layout" button (settings_overlay.cpp).
extern "C" void AndroidStartTouchLayoutEdit() {
    CallVoidMethodOnActivity("onNativeStartTouchLayoutEdit");
}

// Called from the ImGui settings sidebar's "Motion Steering" button (settings_overlay.cpp).
extern "C" void AndroidShowMotionSteeringDialog() {
    CallVoidMethodOnActivity("onNativeShowMotionSteeringDialog");
}

// Available from either standalone product: the installed Retro Rewind pack is shared. Keep JNI
// off the guest fiber and let MainActivity perform the actual navigation on Android's UI thread.
extern "C" void AndroidOpenRetroRewindUpdates() {
    CallVoidMethodOnActivity("openRetroRewindUpdates");
}

// Called from the settings sidebar's "Main Menu" button (settings_overlay.cpp).
extern "C" void AndroidReturnToMainMenu() {
    CallVoidMethodOnActivity("returnToMainMenu");
}

// Called once from MainActivity.showGameUi() right after creating the touch overlay, so the
// settings checkbox above starts in sync with whatever the user last chose (persisted Kotlin-side
// - see TouchControlsOverlay.kt) instead of defaulting to "on" every launch.
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_MainActivity_nativeSetTouchControlsVisibleCache(JNIEnv*, jobject /* this */,
                                                                                jboolean visible) {
    g_androidTouchControlsVisibleCache = (visible == JNI_TRUE);
}

// Same seeding purpose as nativeSetTouchControlsVisibleCache above, for the double-tap-auto-hold
// preference.
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_MainActivity_nativeSetDoubleTapAutoHoldCache(JNIEnv*, jobject /* this */,
                                                                             jboolean enabled) {
    g_androidDoubleTapAutoHoldCache = (enabled == JNI_TRUE);
}

// Called from MainActivity's AudioManager.OnAudioFocusChangeListener - Android's real equivalent
// of the Windows-only media-session monitor in music_attenuation.cpp. This direction (Java calling
// an exported native function) runs on whichever normal Java thread triggered it, with its own
// proper native call frame - unlike the native->Java direction (CallVoidMethodOnActivity above),
// there is no guest-fiber JNI hazard here, so no AndroidJniDispatch involved.
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_MainActivity_nativeReportExternalMediaPlaying(JNIEnv*, jobject /* this */,
                                                                             jboolean playing) {
    MusicAttenuation::ReportExternalMediaPlaying(playing == JNI_TRUE);
}

// Thermal headroom poll from ThermalMonitor.kt; the runtime decides what to do with it.
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_MainActivity_nativeReportThermalHeadroom(JNIEnv*, jobject /* this */,
                                                                        jfloat headroom) {
    ThermalQuality::ReportHeadroom(static_cast<float>(headroom));
}

extern "C" int MkwHostCpuBaselineInit();
int RuntimeMain(int argc, char** argv);  // NOT extern "C" - matches its real declaration in main.cpp

// Touch equivalent of the desktop F10 settings-overlay hotkey - see MainActivity.kt's gear button.
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_MainActivity_nativeToggleSettingsOverlay(JNIEnv*, jobject /* this */) {
    settings_overlay::ToggleTopBar();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_MainActivity_nativeRealRuntimeCheck(JNIEnv* env, jobject /* this */,
                                                                   jstring filesDir) {
    std::string result = "Real libWiiCompiled.so loaded via JNI.\n";

    const char* filesDirChars = env->GetStringUTFChars(filesDir, nullptr);
    RuntimeConfigFile::SetAndroidFilesDir(filesDirChars);
    env->ReleaseStringUTFChars(filesDir, filesDirChars);

    result += "Product: ";
    result += RuntimeProduct::Active().displayName;
    result += "\n";

    result += "App data dir: ";
    result += RuntimeConfigFile::ApplicationDataDirectory().string();
    result += "\n";

    result += "Config path: ";
    result += RuntimeConfigFile::ResolveConfigPath().string();
    result += "\n";

    const int baselineResult = MkwHostCpuBaselineInit();
    result += "CPU baseline check: ";
    result += (baselineResult == 0) ? "OK\n" : "FAILED\n";

    result += "\nThis proves the real engine (with your actual translated game code) loads and "
              "runs on this device. It does not start the game yet - that needs the on-device "
              "install pipeline, which isn't built yet.";

    return env->NewStringUTF(result.c_str());
}

// Paths handed over from Kotlin before MainActivity (now an SDLActivity subclass) calls
// super.onCreate(), i.e. before SDL's own SDLMain thread can possibly start - see
// nativeSetInstallPaths below. Plain globals, not synchronized: written exactly once from the
// UI thread, read exactly once from the SDLMain thread that starts strictly after.
namespace {
std::string g_androidDvdRoot;
std::string g_androidRetroRewindRoot;
std::vector<std::string> g_androidOverlayRoots;
bool g_androidDetectedWidescreen = true;
}  // namespace

// Separate setter (rather than a 3rd nativeSetInstallPaths parameter) so the base-product build
// path never needs to pass anything for it. Same ordering rule as nativeSetInstallPaths: must be
// called before super.onCreate() lets SDLMain's thread start.
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_MainActivity_nativeSetRetroRewindRoot(JNIEnv* env, jobject /* this */,
                                                                     jstring retroRewindRoot) {
    const char* chars = env->GetStringUTFChars(retroRewindRoot, nullptr);
    g_androidRetroRewindRoot = chars;
    env->ReleaseStringUTFChars(retroRewindRoot, chars);
}

// Registers one user-imported texture pack directory (Riivolution-shaped: a riivolution/*.xml
// plus its replacement files, or - as of the by-filename fallback in dvd.cpp's ScanOverlayRoot -
// just a loose file with no XML at all) as an extra DVD overlay root - see ModManager.kt,
// which calls this once per enabled pack, in a loop, before super.onCreate(). Same staging pattern
// as g_androidRetroRewindRoot above, not a direct RuntimeConfigFile::Mutable() push: confirmed
// on-device (matching &Get() pointers logged from both ends, ruling out a second config instance)
// that a direct in-memory push here gets silently wiped moments later - SDL_main below reloads the
// whole RuntimeUserConfig straight from Config.toml on disk (the "Force a reload from the file we
// just wrote" block a few lines down, added earlier for dvd_root/retro_rewind_root) and overwrites
// it, discarding anything that was only ever pushed in memory and never written to the file.
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_MainActivity_nativeAddOverlayRoot(JNIEnv* env, jobject /* this */,
                                                                  jstring overlayRoot) {
    const char* chars = env->GetStringUTFChars(overlayRoot, nullptr);
    g_androidOverlayRoots.emplace_back(chars);
    env->ReleaseStringUTFChars(overlayRoot, chars);
}

// Staged from the device's real physical screen aspect ratio (MainActivity.kt); only ever
// consulted in SDL_main below as a first-run default, and only if the user has never explicitly
// picked an aspect ratio via the settings sidebar (Config.toml has no [video] widescreen key yet).
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_MainActivity_nativeSetDetectedWidescreen(JNIEnv*, jobject /* this */,
                                                                         jboolean widescreen) {
    g_androidDetectedWidescreen = (widescreen == JNI_TRUE);
}

// Only meaningful (and only linked) for the combined libGameCombined.so, where both profiles'
// translated code live in one binary and RuntimeProduct::Active() reads a value set here instead
// of being a compile-time constant per product .so (see runtime_product.h and
// src/product/combined_product.cpp). Same ordering rule as nativeSetInstallPaths above: must be
// called before super.onCreate() lets SDLMain's thread start, since guest boot reads Active()
// immediately. A no-op / link error on the old separate libWiiCompiled.so/libRetroRewind.so
// build - those still link base_product.cpp/retro_rewind_product.cpp, which don't define
// RuntimeProduct::SetActive at all, by design (see runtime_product.h's comment on SetActive).
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_MainActivity_nativeSetActiveProduct(JNIEnv* /* env */, jobject /* this */,
                                                                    jboolean retroRewind) {
    RuntimeProduct::SetActive(retroRewind ? RuntimeProduct::Kind::RetroRewind
                                           : RuntimeProduct::Kind::BaseGame);
}

extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_MainActivity_nativeSetInstallPaths(JNIEnv* env, jobject /* this */,
                                                                  jstring filesDir, jstring dvdRoot) {
    const char* filesDirChars = env->GetStringUTFChars(filesDir, nullptr);
    RuntimeConfigFile::SetAndroidFilesDir(filesDirChars);
    env->ReleaseStringUTFChars(filesDir, filesDirChars);

    const char* dvdRootChars = env->GetStringUTFChars(dvdRoot, nullptr);
    g_androidDvdRoot = dvdRootChars;
    env->ReleaseStringUTFChars(dvdRoot, dvdRootChars);
}

// RomImportOverlay calls this from a background thread it owns (extraction of a multi-GB disc
// is far too slow for the UI thread) and polls the two progress getters below from a timer on
// the main thread meanwhile. Returns null on success, or a human-readable error string.
extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_rom_RomImportOverlay_nativeExtractDisc(JNIEnv* env, jobject /* this */,
                                                                      jint sourceFd,
                                                                      jstring destDataFolder) {
    const char* destChars = env->GetStringUTFChars(destDataFolder, nullptr);
    char error[512] = {};
    const int result = WiiDiscExtractor_Extract(sourceFd, destChars, error, sizeof(error));
    env->ReleaseStringUTFChars(destDataFolder, destChars);
    return result == 0 ? nullptr : env->NewStringUTF(error);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_driftdroid_android_rom_RomImportOverlay_nativeExtractBytesDone(JNIEnv*, jobject) {
    return static_cast<jlong>(WiiDiscExtractor_BytesDone());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_driftdroid_android_rom_RomImportOverlay_nativeExtractBytesTotal(JNIEnv*, jobject) {
    return static_cast<jlong>(WiiDiscExtractor_BytesTotal());
}

// The real SDL3 Android entry point. MainActivity now extends SDLActivity (see MainActivity.kt),
// so SDL's own Java glue drives the whole lifecycle we used to hand-roll here: it calls
// SDL.setupJNI()/SDL.initialize() from onCreate() (populating the mActivityClass/method-ID
// globals SDL_VideoInit's touch-input setup needs - see the git history of this file for the
// on-device tombstone that showed why that call is mandatory), creates the SDLSurface and hands
// SDL a real ANativeWindow, then - once the surface is ready and the activity has focus - spawns
// its own background SDLMain thread and dlsym's+calls exactly this function by name ("SDL_main"
// is SDLActivity.getMainFunction()'s default). No manual SDL_SetMainReady() or extra thread
// needed: SDL_RunApp (which nativeRunMain calls into) already does the real equivalent of both.
extern "C" int SDL_main(int argc, char** argv) {
    const std::filesystem::path configPath = RuntimeConfigFile::ResolveConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(configPath.parent_path(), ec);

    // Set dvd_root/retro_rewind_root via the same read-modify-write path the in-game
    // settings sidebar uses (RuntimeConfigFile::WriteSetting), NOT a raw truncating overwrite.
    // The truncating version this replaced destroyed every other saved setting - audio volumes,
    // the "mute music while external media plays" toggle, resolution scale, controller mappings,
    // all of it - on EVERY single app launch, before InitializeRuntimeSettings() ever got a chance
    // to read them back. Confirmed directly: a toggle enabled one session showed disabled again
    // next time, which is not "didn't save" so much as "got wiped a few hundred milliseconds after
    // boot, before you could even background the app." dvd_root itself needs a real, uncommented
    // value even on a first launch with no config file yet, which is exactly what WriteSetting
    // already provides (EnsureConfigFile leaves it commented out).
    //
    // graphics_api is deliberately NOT written here. It used to be, pinned to "auto", which
    // silently undid the renderer the user picked in the launcher (GraphicsSettings.kt writes it
    // into this same file) on every single launch - choose OpenGL ES, and the game still came up
    // on Vulkan. EnsureConfigFile's template already seeds "auto" for a brand new config, so
    // there is nothing left for launch to do here.
    RuntimeConfigFile::WriteSetting("paths", "dvd_root", "\"" + g_androidDvdRoot + "\"");
    if (!g_androidRetroRewindRoot.empty()) {
        RuntimeConfigFile::WriteSetting("paths", "retro_rewind_root", "\"" + g_androidRetroRewindRoot + "\"");
    }
    // Always written (even empty, as "[]") rather than only when non-empty like
    // retro_rewind_root above - this one really can need to shrink back to zero, e.g. the user
    // disabled every texture pack since the last launch, and an empty write is what actually
    // clears whatever a PREVIOUS launch persisted here (Kotlin re-sends the current enabled set
    // in full every launch - see ModManager.enabledOverlayRoots - so this is always
    // authoritative, never something to merge with the old file value).
    {
        std::string joined = "[";
        for (const std::string& root : g_androidOverlayRoots) {
            if (joined.size() > 1) {
                joined += ", ";
            }
            std::string escaped;
            for (char c : root) {
                if (c == '\\' || c == '"') {
                    escaped += '\\';
                }
                escaped += c;
            }
            joined += "\"" + escaped + "\"";
        }
        joined += "]";
        RuntimeConfigFile::WriteSetting("paths", "overlay_roots", joined);
    }
    // Only a FIRST-RUN default: if a previous session (or this same one, via the settings
    // sidebar's "Aspect Ratio" picker) already wrote an explicit [video] widescreen value, this
    // must never override it - Get() here reflects whatever was on disk when this library was
    // loaded (see the comment on the reload block right below), which already includes anything
    // an earlier session wrote. Real device report: without this, every device defaulted to
    // widescreen=true regardless of actually having a 4:3 screen (Retroid Pocket Nova), so the
    // game's own HUD/menu layout math (SCGetAspectRatio_HLE, hle/sc.cpp) always assumed 16:9.
    if (!RuntimeConfigFile::Get().widescreen.has_value()) {
        RuntimeConfigFile::WriteSetting("video", "widescreen", g_androidDetectedWidescreen ? "true" : "false");
    }

    // RuntimeConfigFile::Get() memoizes into a function-local static on its first call, and
    // several unrelated globals (see settings_overlay.cpp: g_resolutionScale, g_audioMuted, etc.)
    // are themselves initialized directly from Get() at static-init time - which on Android fires
    // the instant System.loadLibrary("WiiCompiled") runs in MainActivity's companion object, well
    // before this function gets a chance to write the real Config.toml above. That first, early
    // Get() call reads whatever (nonexistent/default) file happens to exist at that moment and
    // freezes it for the rest of the process - confirmed on-device: dvd_root was never seen despite
    // being correctly present on disk. Force a reload from the file we just wrote, now that it
    // exists, before any real config consumer (DVD/NAND init, etc.) runs. This does not fix the
    // settings_overlay.cpp globals themselves (already-baked plain bool/float copies, not
    // references), only Get()'s own storage - sufficient for dvd_root/nand_root, which are read
    // live through Get() rather than cached into a copy.
    {
        std::ifstream reload(configPath, std::ios::binary);
        if (reload) {
            RuntimeConfigFile::Mutable() = RuntimeConfigFile::ParseConfig(reload, configPath.string());
        }
    }

    // Redirect stdout/stderr - RuntimeMain and everything it calls logs through these, not
    // through __android_log_print, so logcat alone won't show it. freopen()-ing stderr onto the
    // same path a second time (as this used to do) opens a SECOND, independent fd at its own
    // offset - two unbuffered writers racing on the same file then corrupt each other's bytes
    // (confirmed on-device: an "Executed 43 main DOL static constructors." line came back
    // truncated mid-word with an unrelated line spliced into it). dup2 makes stderr share
    // stdout's exact fd/offset instead, so every write - regardless of which stream - is a single
    // ordered sequence.
    const std::filesystem::path logPath = configPath.parent_path() / "android_runtime_attempt.log";
    std::freopen(logPath.string().c_str(), "w", stdout);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    dup2(fileno(stdout), fileno(stderr));
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    std::printf("[android_jni_bridge] SDL_main -> RuntimeMain\n");
    std::fflush(stdout);

    const int result = RuntimeMain(argc, argv);

    std::printf("[android_jni_bridge] RuntimeMain returned %d\n", result);
    std::fflush(stdout);
    return result;
}

#endif  // __ANDROID__
