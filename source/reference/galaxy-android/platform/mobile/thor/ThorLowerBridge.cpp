// AYN Thor lower screen: the platform half of the Galaxy state bridge.
//
// The game thread commits one ThorLowerState per frame (decomp/src/Game/System/ThorLowerState.cpp)
// into a seqlocked static buffer; the Java lower screen copies it out through
// GalaxyActivity.nativeLowerState, registered here with RegisterNatives because libgame.so is
// loaded RTLD_LOCAL into its reserved window (platform/android/launcher.c), where the VM's own
// JNI symbol lookup cannot see it. No heap allocation, no game pointers cross to Java.
#include "ThorLowerBridge.h"

#include <android/log.h>
#include <string.h>

#include "port/thor_lower.h"
#include "port/thor_world.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyThorLower", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "GalaxyThorLower", __VA_ARGS__)

// The live space picture and the upper screen's bright level (platform/src/gx/vk/vk_thor_capture.cpp).
extern "C" uint32_t port_thor_space_copy(uint8_t* out, int size, uint32_t have, int* mode);
extern "C" int port_thor_top_level(void);

namespace {
    ThorLowerState sPublished;

    // 620x540 RGBA, rows top first; returns its sequence (0 when nothing newer than `have`),
    // with its kind in mode[0] (1, the title: the only picture taken). mode[1], every call: the
    // upper screen's bright level (0..255, -1 before the first measurement).
    jint JNICALL nativeSpace(JNIEnv* env, jclass, jobject buffer, jint have, jintArray mode) {
        if (buffer == nullptr) return 0;
        void* out = env->GetDirectBufferAddress(buffer);
        if (out == nullptr) return 0;
        int captured = 0;
        const uint32_t seq = port_thor_space_copy(static_cast<uint8_t*>(out), (int)env->GetDirectBufferCapacity(buffer),
                                                  static_cast<uint32_t>(have), &captured);
        if (seq != 0 && mode != nullptr && env->GetArrayLength(mode) >= 1) {
            jint m = captured;
            env->SetIntArrayRegion(mode, 0, 1, &m);
        }
        if (mode != nullptr && env->GetArrayLength(mode) >= 2) {
            jint level = port_thor_top_level();
            env->SetIntArrayRegion(mode, 1, 1, &level);
        }
        return static_cast<jint>(seq);
    }
    uint32_t sSeq;  // odd while the game thread writes

    jint JNICALL nativeLowerState(JNIEnv* env, jclass, jobject buffer) {
        if (buffer == nullptr) return 0;
        void* out = env->GetDirectBufferAddress(buffer);
        if (out == nullptr || env->GetDirectBufferCapacity(buffer) < (jlong)sizeof(ThorLowerState)) return 0;
        for (int tries = 0; tries < 8; tries++) {
            const uint32_t before = __atomic_load_n(&sSeq, __ATOMIC_ACQUIRE);
            if (before == 0 || (before & 1)) continue;
            memcpy(out, &sPublished, sizeof(ThorLowerState));
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (__atomic_load_n(&sSeq, __ATOMIC_ACQUIRE) == before) return (jint)(before >> 1);
        }
        return 0;
    }
}  // namespace

// The stage's live world pieces (decomp/src/Game/System/ThorWorldState.cpp), seqlocked as above.
namespace {
    alignas(8) uint8_t sWorld[THOR_WORLD_SIZE];
    uint32_t sWorldBytes;
    uint32_t sWorldSeq;  // odd while the game thread writes
}  // namespace

extern "C" void port_thor_world_commit(const ThorWorldHeader* header, const ThorWorldPiece* pieces, uint32_t count) {
    if (header == nullptr) return;
    if (count > THOR_WORLD_PIECES) count = THOR_WORLD_PIECES;
    const uint32_t seq = __atomic_load_n(&sWorldSeq, __ATOMIC_RELAXED);
    __atomic_store_n(&sWorldSeq, seq + 1, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    memcpy(sWorld, header, sizeof(ThorWorldHeader));
    reinterpret_cast<ThorWorldHeader*>(sWorld)->count = count;
    if (count > 0) memcpy(sWorld + sizeof(ThorWorldHeader), pieces, count * sizeof(ThorWorldPiece));
    sWorldBytes = static_cast<uint32_t>(sizeof(ThorWorldHeader) + count * sizeof(ThorWorldPiece));
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&sWorldSeq, seq + 2, __ATOMIC_RELEASE);
}

extern "C" uint32_t port_thor_world_copy(uint8_t* out, int size, uint32_t have) {
    if (out == nullptr || size < (int)sizeof(ThorWorldHeader)) return 0;
    for (int tries = 0; tries < 8; tries++) {
        const uint32_t before = __atomic_load_n(&sWorldSeq, __ATOMIC_ACQUIRE);
        if (before == 0) return 0;
        if (before & 1) continue;
        if ((before >> 1) == have) return 0;  // nothing newer
        uint32_t bytes = sWorldBytes;
        if (bytes > (uint32_t)size)
            bytes = (uint32_t)(sizeof(ThorWorldHeader) + (size - sizeof(ThorWorldHeader)) / sizeof(ThorWorldPiece) * sizeof(ThorWorldPiece));
        memcpy(out, sWorld, bytes);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&sWorldSeq, __ATOMIC_ACQUIRE) == before) {
            ThorWorldHeader* h = reinterpret_cast<ThorWorldHeader*>(out);
            const uint32_t fits = (bytes - sizeof(ThorWorldHeader)) / sizeof(ThorWorldPiece);
            if (h->count > fits) h->count = fits;
            return before >> 1;
        }
    }
    return 0;
}

namespace {
    // The world state into a direct buffer; its sequence, or 0 when nothing is newer than `have`.
    jint JNICALL nativeWorldState(JNIEnv* env, jclass, jobject buffer, jint have) {
        if (buffer == nullptr) return 0;
        void* out = env->GetDirectBufferAddress(buffer);
        if (out == nullptr) return 0;
        return static_cast<jint>(port_thor_world_copy(static_cast<uint8_t*>(out), (int)env->GetDirectBufferCapacity(buffer),
                                                      static_cast<uint32_t>(have)));
    }
}  // namespace

extern "C" void port_thor_lower_commit(const ThorLowerState* state) {
    if (state == nullptr) return;
    const uint32_t seq = __atomic_load_n(&sSeq, __ATOMIC_RELAXED);
    __atomic_store_n(&sSeq, seq + 1, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    memcpy(&sPublished, state, sizeof(ThorLowerState));
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&sSeq, seq + 2, __ATOMIC_RELEASE);
}

namespace mobile_thor {

void registerLowerBridge(ANativeActivity* activity) {
    if (activity == nullptr || activity->vm == nullptr || activity->clazz == nullptr) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint got = activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (got == JNI_EDETACHED) {
        if (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr) return;
        attached = true;
    } else if (got != JNI_OK || env == nullptr) {
        return;
    }
    jclass clazz = env->GetObjectClass(activity->clazz);
    if (clazz != nullptr) {
        static const JNINativeMethod methods[] = {
            {"nativeLowerState", "(Ljava/nio/ByteBuffer;)I", (void*)nativeLowerState},
            {"nativeSpace", "(Ljava/nio/ByteBuffer;I[I)I", (void*)nativeSpace},
            {"nativeWorldState", "(Ljava/nio/ByteBuffer;I)I", (void*)nativeWorldState},
        };
        // One at a time: an older helper without a declaration loses only that method.
        for (const JNINativeMethod& method : methods) {
            if (env->RegisterNatives(clazz, &method, 1) == JNI_OK) {
                LOGI("lower screen bridge: %s registered (state %zu bytes, version %d)", method.name, sizeof(ThorLowerState),
                     THOR_LOWER_VERSION);
            } else {
                if (env->ExceptionCheck()) env->ExceptionClear();
                LOGW("lower screen bridge: %s not registered", method.name);
            }
        }
        env->DeleteLocalRef(clazz);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) activity->vm->DetachCurrentThread();
}

}  // namespace mobile_thor
