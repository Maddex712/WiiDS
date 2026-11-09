#include "lower_state.h"
#include "runtime.h"
#include <jni.h>

// Install through the game's single guest-frame hook, alongside its other observers.
// Never call sample() on the Android/UI/render threads.
void wiids_mkdd_guest_frame(uint32_t frame) {
    const wiids::mkdd::Memory memory(g_mem, RAM_SIZE);
    auto packet = wiids::mkdd::sample(memory, frame);
#ifdef WIIDS_MKDD_PLUS
    // Plus 3.3's course-set byte, read by its switch routine at 0x803e231c.
    // It is sampled on the guest thread with the rest of the frame, never from Java.
    const auto page = memory.u8(0x802ed5f9);
    if (page <= 4) packet[wiids::mkdd::CourseSet] = page;
    else packet = wiids::mkdd::unavailable(frame);
#endif
    wiids::mkdd::publish(packet);
}

extern "C" JNIEXPORT jintArray JNICALL
Java_dev_thor_mkdd_MkddNative_readLowerState(JNIEnv* env, jclass) {
    auto packet = wiids::mkdd::latest();
    jintArray result = env->NewIntArray(packet.size());
    if (result) env->SetIntArrayRegion(result, 0, packet.size(), packet.data());
    return result;
}
