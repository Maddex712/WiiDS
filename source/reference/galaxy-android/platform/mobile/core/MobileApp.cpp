#include "port/profile.h"
#include "port/kaizo.h"
#include "MobileApp.h"

#include <android/log.h>
#include <android/keycodes.h>
#include <atomic>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include "GameBooter.h"
#include "../input/Sensors.h"
#include "../render/ScenePresenter.h"
#include "../render/VulkanLifecycle.h"
#include "../system/StorageManager.h"
#include "../system/SystemManager.h"
#include "../thor/ThorLowerBridge.h"
#include "../thor/ThorSettingsBridge.h"
#include "../ui/UIOverlay.h"
#include "port/port.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyCore", __VA_ARGS__)
#ifndef LOGW
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "GalaxyCore", __VA_ARGS__)
#endif
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GalaxyCore", __VA_ARGS__)

extern "C" void port_mem_set_reserved_window(uintptr_t base, size_t size);
extern "C" void port_boot(const char* dataRoot, const char* saveRoot);
extern "C" void port_audio_pause(void);
extern "C" void port_audio_resume(void);
extern std::atomic<uint64_t> gPresentedFrames;

namespace mobile_core {

MobileApp& MobileApp::get() {
    static MobileApp sInstance;
    return sInstance;
}

void MobileApp::handleAppCmd(int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW: {
        LOGI("Initializing Vulkan 1.0 native renderer...");
        if (!mobile_render::VulkanLifecycle::bringUp(mAndroid->window, mScreenWidth, mScreenHeight, mVkTarget, mVpMgr)) {
            LOGE("FATAL: Vulkan initialization failed. Device lacks required Vulkan support.");
            mVkReady = false;
            mWindowReady = false;
            break;
        }
        mVkReady = true;
        mobile_system::SystemManager::enforceOrientation(mAndroid, mobile_ui::UIOverlay::get().settings().screenOrientationChoice);
        int initialFps = mobile_ui::UIOverlay::get().settings().targetFps;
        if (mobile_ui::UIOverlay::get().settings().batterySaver) initialFps = 30;
        mobile_system::SystemManager::setPreferredRefreshRate(mAndroid, (float)initialFps);
        mWindowReady = true;
        break;
    }
    case APP_CMD_TERM_WINDOW:
        LOGI("APP_CMD_TERM_WINDOW");
        mobile_ui::UIOverlay::get().resetInput();
        if (mVkReady) {
            if (mVkTarget.framebuffer) gpu::VkRenderer::get().destroyTarget(mVkTarget);
            gpu::VkRenderer::get().shutdown();
            mVkReady = false;
        }
        mWindowReady = false;
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        LOGI("Window resized / config changed");
        if (mVkReady) {
            gpu::VkRenderer::get().onWindowResized(mAndroid->window);
            mScreenWidth = ANativeWindow_getWidth(mAndroid->window);
            mScreenHeight = ANativeWindow_getHeight(mAndroid->window);
            mVpMgr.update(mScreenWidth, mScreenHeight, mobile_ui::UIOverlay::get().settings().fullscreen);
        }
        break;
    case APP_CMD_RESUME:
        LOGI("APP_CMD_RESUME");
        // The settings menu keeps the game frozen until it closes.
        if (!mobile_thor::settingsMenuOpen()) {
            port_audio_resume();
            port_set_paused(0);
        }
        break;
    case APP_CMD_PAUSE:
        LOGI("APP_CMD_PAUSE");
        mobile_thor::resetTouchPointer();
        if (mVkReady) gpu::VkRenderer::get().savePipelineCache();
        mobile_ui::UIOverlay::get().resetInput();
        mGamepad.reset();
        mSpinFrames = 0;
        mTouchscreenDown = false;
        mPad.buttons = mobile_ui::UIOverlay::get().getTouchButtons();
        port_input_set(0, &mPad);
        port_audio_pause();
        port_set_paused(1);
        break;
    case APP_CMD_DESTROY:
        LOGI("APP_CMD_DESTROY");
        if (mVkReady) {
            if (mVkTarget.framebuffer) gpu::VkRenderer::get().destroyTarget(mVkTarget);
            gpu::VkRenderer::get().shutdown();
            mVkReady = false;
        }
        mWindowReady = false;
        break;
    default:
        break;
    }
}

int32_t MobileApp::handleInputEvent(AInputEvent* event) {
    int32_t eventType = AInputEvent_getType(event);
    if (eventType == AINPUT_EVENT_TYPE_MOTION) {
        if (mGamepad.handleMotionEvent(event, mPad, mSpinFrames, mTouchscreenDown)) {
            updateInputState();
            return 1;
        }

        int32_t touchAction = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (touchAction == AMOTION_EVENT_ACTION_DOWN) mTouchscreenDown = true;
        if (touchAction == AMOTION_EVENT_ACTION_UP || touchAction == AMOTION_EVENT_ACTION_CANCEL) mTouchscreenDown = false;

        mVpMgr.update(mScreenWidth, mScreenHeight, mobile_ui::UIOverlay::get().settings().fullscreen);
        mobile_display::Rect ptrBounds;
        mobile_display::ViewportManager::computePointerBounds(
            mScreenWidth, mScreenHeight, mobile_ui::UIOverlay::get().settings().fullscreen,
            mVpMgr.getViewport(), ptrBounds);

        if (mobile_ui::UIOverlay::get().handleInput(event, mPad, mSpinFrames,
                                                    mScreenWidth, mScreenHeight,
                                                    ptrBounds.x, ptrBounds.y, ptrBounds.w, ptrBounds.h)) {
            updateInputState();
            return 1;
        }
    } else if (eventType == AINPUT_EVENT_TYPE_KEY) {
        const int key = AKeyEvent_getKeyCode(event);
        if (key == AKEYCODE_BACK || key == AKEYCODE_ESCAPE) {
            // AYN Thor: Back opens the Galaxy settings menu (its own row returns to WiiDS); without
            // the helper's menu it returns to WiiDS as before.
            if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP && AKeyEvent_getRepeatCount(event) == 0) {
                if (!mobile_thor::showSettingsMenu(mAndroid->activity))
                    mobile_system::SystemManager::returnToLauncher(mAndroid);
            }
            return 1;
        }
        if (mGamepad.handleKeyEvent(event, mPad, mSpinFrames)) {
            updateInputState();
            return 1;
        }
    }
    return 0;
}

// AYN Thor settings menu requests from Java (platform/mobile/thor/ThorSettingsBridge.cpp), on this
// thread. Opening freezes the game and its audio and hands the game a neutral pad (the menu's own
// window takes the controls); closing resumes it with no button or stick carried over.
void MobileApp::applyThorMenuRequests() {
    mobile_thor::ThorMenuRequest request;
    while (mobile_thor::takeMenuRequest(request)) {
        if (request.kind == mobile_thor::THOR_MENU_SET) {
            mobile_thor::applySetting(request.key, request.value);
            continue;
        }
        const bool open = request.kind == mobile_thor::THOR_MENU_OPEN;
        if (open == mobile_thor::settingsMenuOpen()) continue;
        mobile_thor::resetTouchPointer();
        mGamepad.reset();
        mSpinFrames = 0;
        mPad.buttons = 0;
        mPad.stickX = mPad.stickY = 0.0f;
        mPad.rightStickX = mPad.rightStickY = 0.0f;
        port_input_set(0, &mPad);
        if (open) {
            mobile_thor::setSettingsMenuOpen(true);
            port_audio_pause();
            port_set_paused(1);
            LOGI("settings menu open: game frozen");
        } else {
            mobile_ui::UIOverlay::get().saveSettings();
            mobile_thor::setSettingsMenuOpen(false);
            port_audio_resume();
            port_set_paused(0);
            LOGI("settings menu closed: game resumed");
        }
    }
    // Quick Save from the menu: once the game runs again (the save happens in the game's frame).
    if (!mobile_thor::settingsMenuOpen() && mobile_thor::takeQuickSave()) {
        mobile_ui::UIOverlay::get().triggerSaveAnywhere();
        LOGI("settings menu: quick save requested");
    }
}

void MobileApp::updateInputState() {
    if (mobile_ui::UIOverlay::get().isStickActive()) {
        mPad.stickX = mobile_ui::UIOverlay::get().getTouchStickX();
        mPad.stickY = mobile_ui::UIOverlay::get().getTouchStickY();
    } else if (mGamepad.isGamepadDetected() && (fabsf(mGamepad.stickX()) > 0.001f || fabsf(mGamepad.stickY()) > 0.001f)) {
        mPad.stickX = mGamepad.stickX();
        mPad.stickY = mGamepad.stickY();
    } else {
        mPad.stickX = 0.0f;
        mPad.stickY = 0.0f;
    }
    mPad.buttons = mGamepad.buttons() | mobile_ui::UIOverlay::get().getTouchButtons();
    port_input_set(0, &mPad);
}



void MobileApp::run(android_app* app, uintptr_t windowBase, size_t windowSize) {
    mobile_system::SystemManager::installCrashHandlers();
    mobile_system::SystemManager::elevateThreadPriority(-10);
    mobile_system::SystemManager::pinThreadToPerformanceCores();

    mAndroid = app;
    app->userData = this;
    app->onAppCmd = [](android_app* a, int32_t cmd) {
        MobileApp::get().handleAppCmd(cmd);
    };
    app->onInputEvent = [](android_app* a, AInputEvent* ev) -> int32_t {
        return MobileApp::get().handleInputEvent(ev);
    };

    mobile_input::Sensors::get().init(app);
    port_mem_set_reserved_window(windowBase, windowSize);
    // AYN Thor lower screen: lets GalaxyActivity copy the game's state snapshot.
    mobile_thor::registerLowerBridge(app->activity);
    mobile_thor::registerSettingsBridge(app->activity);

    mPad.connected = 1;
    port_input_set(0, &mPad);
    port_skip_set_hold_seconds(0.0f);

    std::string internalPath = app->activity->internalDataPath ? app->activity->internalDataPath : port_profile_internal_root();
    bool isPlus = (internalPath.find("plus") != std::string::npos);
    mobile_ui::UIOverlay::get().setPlusEdition(isPlus);

    std::string ext = app->activity->externalDataPath ? app->activity->externalDataPath : std::string(port_profile_data_root()).substr(0, std::string(port_profile_data_root()).size() - 11);
    mSaveRoot = internalPath + "/nand";
    mkdir(mSaveRoot.c_str(), 0770);
    mobile_ui::UIOverlay::get().setSavePath(mSaveRoot + (isPlus ? "/touch_settings_plus.ini" : "/touch_settings.ini"));

    int initialFps = mobile_ui::UIOverlay::get().settings().targetFps;
    if (mobile_ui::UIOverlay::get().settings().batterySaver) initialFps = 30;
    mobile_system::SystemManager::setPreferredRefreshRate(app, (float)initialFps);

    std::string primaryLog = ext + "/GalaxyData/galaxy_log.txt";
    FILE* tst = fopen(primaryLog.c_str(), "a");
    if (tst) {
        fclose(tst);
        port_log_file(primaryLog.c_str());
    } else {
        port_log_file((ext + "/galaxy_log.txt").c_str());
    }

    mobile_render::VulkanLifecycle::initCaches(internalPath);

    mDataRoot = mobile_system::StorageManager::findGameData(ext);
    if (mDataRoot.empty()) {
        mobile_system::StorageManager::promptStoragePermissionIfMissing(app->activity);
    }

    bool running = true;
    int64_t previousInputTime = port_host_time_ns();
    while (running && !app->destroyRequested) {
        int events;
        android_poll_source* source = nullptr;
        int timeout = (mWindowReady) ? 0 : 50;
        int ident;
        while ((ident = ALooper_pollOnce(timeout, nullptr, &events, (void**)&source)) >= 0) {
            if (ident == 3) {
                mobile_input::Sensors::get().poll(mPad, mobile_ui::UIOverlay::get().isStickActive());
            }
            if (source) {
                source->process(app, source);
            }
            if (app->destroyRequested) {
                running = false;
                break;
            }
            timeout = 0;
        }

        applyThorMenuRequests();

        if (!mWindowReady || port_is_paused()) {
            usleep(mobile_thor::settingsMenuOpen() ? 8000 : 25000);
            continue;
        }

        mobile_core::GameBooter::pollAndBoot(mGameBooted, mDataRoot, mSaveRoot, ext, mAndroid);

        mobile_ui::UIOverlay::get().update(mPad, mSpinFrames);
        float lowerX = mobile_ui::UIOverlay::get().getPointerX();
        float lowerY = mobile_ui::UIOverlay::get().getPointerY();
        const bool trackpadTouch = mobile_thor::applyTouchTrackpad(lowerX, lowerY);
        const bool lowerTouch = trackpadTouch || mobile_thor::readTouchPointer(lowerX, lowerY);
        if (lowerTouch) {
            mobile_ui::UIOverlay::get().setPointerPos(lowerX, lowerY);
            mPad.pointerX = lowerX;
            mPad.pointerY = lowerY;
            mPad.pointerDist = 2.f;
            mPad.pointerValid = 1;
        } else if (mLowerTouchWasDown && !mTouchscreenDown) {
            mobile_ui::UIOverlay::get().releasePointer();
            mPad.pointerValid = 0;
        }
        mLowerTouchWasDown = lowerTouch;
        const int64_t inputTime = port_host_time_ns();
        mGamepad.updatePointer(mPad, (float)(inputTime - previousInputTime) * 1e-9f, mTouchscreenDown || lowerTouch);
        previousInputTime = inputTime;
        if (mSpinFrames > 0) {
            float s = (mSpinFrames & 1) ? 2.5f : -2.5f;
            mPad.accX = s;
            mPad.accY = s;
            mSpinFrames--;
        } else {
            mPad.accX = 0.0f;
            mPad.accY = 0.0f;
        }
        updateInputState();

        if (mobile_ui::UIOverlay::get().settings().haptics) {
            static int sLastRumbleState = 0;
            int r = port_input_rumble(0);
            if (r && !sLastRumbleState) {
                mobile_system::SystemManager::triggerHaptic(mAndroid, 45);
            }
            sLastRumbleState = r;
        }

        if (mVkReady && gpu::VkRenderer::get().hasFailed()) {
            if (!mobile_render::VulkanLifecycle::handleDeviceLoss(
                    mAndroid->window, mScreenWidth, mScreenHeight, mVkTarget, mVpMgr, mVkReady)) {
                continue;
            }
        }

        if (mVkReady) {
            mobile_render::ScenePresenter::presentVulkan(
                gpu::VkRenderer::get(), mVkTarget, mScreenWidth, mScreenHeight,
                mGameBooted, mVpMgr, mFramePacer, mAndroid);
        }
    }

    LOGI("GalaxyMobile exiting cleanly");
    _exit(0);
}

}  // namespace mobile_core
