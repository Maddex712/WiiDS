#pragma once

// Confirmed via a real Android debuggerd tombstone (not a guess - see settings_overlay.cpp's
// UpdateCursorAutoHide() and the vendored SDL_sysjoystick.c patch for the two call sites that
// actually crashed): a JNI call made while executing on this engine's guest fiber stack
// (fiber_manager.cpp's co_switch()-swapped native stack, not this OS thread's original one)
// corrupts ART's CheckJNI call-frame bookkeeping and aborts with "JNI ERROR (app bug): jstring is
// an invalid JNI transition frame reference". Every native->Java call in this codebase is a live
// instance of this same hazard, whether or not it has been observed crashing yet - ImGui settings
// draw code (where several such calls originate) runs INSIDE the guest fiber's translated
// execution, not on a plain, un-swapped native thread.
//
// Route every native->Java call through here instead of calling JNI directly from wherever guest/
// ImGui code happens to run. Tasks execute on one dedicated, plain pthread with its own untouched
// native stack - Android_JNI_GetEnv()'s AttachCurrentThread-on-demand behavior (used internally by
// SDL_GetAndroidJNIEnv()) makes a fresh pthread a fully standard, safe JNI caller.
#if defined(__ANDROID__)

#include <functional>

namespace AndroidJniDispatch {

// Fire-and-forget: queues task to run on the dispatch thread and returns immediately. Safe to
// call from any thread, including from inside translated guest code. Tasks run in the order
// posted, one at a time, so no external synchronization is needed between two posted tasks meant
// to happen in sequence, but a caller must not depend on the task having run yet by the time
// Post() returns.
void Post(std::function<void()> task);

}  // namespace AndroidJniDispatch

#endif  // __ANDROID__
