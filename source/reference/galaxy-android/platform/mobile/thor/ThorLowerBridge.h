#pragma once

#include <android/native_activity.h>
#include <jni.h>

namespace mobile_thor {

// Registers GalaxyActivity.nativeLowerState (the Thor lower screen's state copy) on the activity's
// class. Call once from the native activity thread before the game boots.
void registerLowerBridge(ANativeActivity* activity);

}  // namespace mobile_thor
