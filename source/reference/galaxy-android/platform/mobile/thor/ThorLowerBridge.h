#pragma once

#include <android/native_activity.h>
#include <jni.h>

namespace mobile_thor {

// Registers GalaxyActivity.nativeLowerState (the Thor lower screen's state copy) on the activity's
// class. Call once from the native activity thread before the game boots.
void registerLowerBridge(ANativeActivity* activity);
bool readTouchPointer(float& x, float& y);
bool applyTouchTrackpad(float& x, float& y);
void resetTouchPointer();

}  // namespace mobile_thor
