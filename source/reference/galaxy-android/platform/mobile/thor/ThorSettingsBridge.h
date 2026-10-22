#pragma once

#include <android/native_activity.h>
#include <jni.h>

// AYN Thor: the Galaxy settings menu (the helper's GalaxySettingsMenu.java), drawn by Java over the
// frozen game. Java talks to it from its UI thread; everything that touches the game or the settings
// is queued here and applied on the native main thread (MobileApp::run drains it every iteration).
namespace mobile_thor {

// The values the menu shows and changes (GalaxySettingsMenu.java mirrors these numbers).
enum ThorSetting {
    THOR_SET_RESOLUTION = 0,      // 0 480p, 1 720p, 2 1080p, 3 1440p
    THOR_SET_FRAME_RATE = 1,      // 60 or 30
    THOR_SET_SMOOTHING = 2,       // anti-aliasing and bloom (the present pass's post-process)
    THOR_SET_SHOW_FPS = 3,        // 0 off, 1 FPS, 2 full stats
    THOR_SET_RIGHT_STICK = 4,     // 0 camera, 1 Star Pointer
    THOR_SET_POINTER_SPEED = 5,   // percent
    THOR_SET_STICK_RESPONSE = 6,  // percent
    THOR_SET_INVERT_CAMERA = 7,
    THOR_SET_LANGUAGE = 8,        // 0 auto, 1 English, 2 Spanish, 3 French, 4 German, 5 Italian (next launch)
    THOR_SET_SKIP_MOVIES = 9,
    THOR_SET_INFINITE_LIVES = 10,
    THOR_SET_INFINITE_HEALTH = 11,
    THOR_SET_INFINITE_STAR_BITS = 12,
    THOR_SET_QUICK_SAVE = 13,     // an action: saves once the menu has closed and the game runs
    THOR_SET_BUTTON_BASE = 14,    // 14..25: what each Thor button does (mobile_input::ButtonAction)
    THOR_SET_BUTTONS_RESET = 26,  // an action: the standard layout
    THOR_SET_COUNT
};

enum ThorMenuRequestKind { THOR_MENU_OPEN, THOR_MENU_CLOSE, THOR_MENU_SET };

struct ThorMenuRequest {
    ThorMenuRequestKind kind;
    int key;
    int value;
};

// Registers GalaxyActivity.nativeMenuState/nativeMenuSet/nativeMenuValues on the activity's class.
void registerSettingsBridge(ANativeActivity* activity);
// Native main thread: the next queued request, if any.
bool takeMenuRequest(ThorMenuRequest& out);
// Whether the menu has the game frozen (native main thread's view).
bool settingsMenuOpen();
void setSettingsMenuOpen(bool open);
// Applies one value to the settings and the engine (native main thread).
void applySetting(int key, int value);
// Whether a Quick Save was chosen in the menu; taking it clears it (native main thread).
bool takeQuickSave();
// Asks Java to show the menu (Back, the gear). False when the helper cannot (an older APK).
bool showSettingsMenu(ANativeActivity* activity);

}  // namespace mobile_thor
