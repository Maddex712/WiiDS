// JNI surface of wiids_disc for the WiiDS library (com.driftdroid.android.rom.WiiDsDisc).
// File descriptors are passed with ownership (ParcelFileDescriptor.detachFd()); they are closed
// here. One extraction runs at a time; progress is polled from the UI thread.
#include "wiids_disc.h"

#include <jni.h>
#include <unistd.h>

#include <exception>
#include <mutex>
#include <string>
#include <vector>

using namespace wiids::disc;

namespace {

Progress g_progress;
std::mutex g_extractLock;

std::vector<std::unique_ptr<Part>> TakeParts(JNIEnv* env, jintArray fds) {
    std::vector<std::unique_ptr<Part>> parts;
    const jsize count = env->GetArrayLength(fds);
    std::vector<jint> values(static_cast<size_t>(count));
    env->GetIntArrayRegion(fds, 0, count, values.data());
    for (size_t i = 0; i < values.size(); ++i) {
        try {
            parts.push_back(OpenFd(values[i]));
        } catch (...) {
            for (size_t j = i + 1; j < values.size(); ++j) close(values[j]);
            throw;
        }
    }
    return parts;
}

// Header bytes and messages may hold anything (Shift-JIS or CP1252 titles, NULs); NewStringUTF
// needs valid Modified UTF-8 and the result is tab-separated, so only printable ASCII passes.
std::string Field(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) c = c == '\t' || c == '\n' ? ' ' : '?';
    return out;
}

// A title that isn't plain ASCII is left out; the app then shows the game ID.
std::string TitleField(const std::string& s) {
    for (char c : s)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return std::string();
    return s;
}

jstring Extract(JNIEnv* env, jintArray fds, jstring dest, bool wii) {
    std::lock_guard<std::mutex> lock(g_extractLock);
    // The cancel flag is cleared when a job starts (nativeResetCancel), not here: a Cancel
    // tapped while the import was still preparing must stop this extraction.
    g_progress.done = 0;
    g_progress.total = 0;
    const char* chars = env->GetStringUTFChars(dest, nullptr);
    const std::string path = chars;
    env->ReleaseStringUTFChars(dest, chars);
    try {
        auto parts = TakeParts(env, fds);
        if (wii) ExtractWii(parts, path, g_progress);
        else ExtractGameCubeIso(parts, path, g_progress);
        return nullptr;
    } catch (const std::exception& e) {
        return env->NewStringUTF(Field(e.what()).c_str());
    } catch (...) {
        return env->NewStringUTF("The disc image couldn't be read.");
    }
}

} // namespace

// "ok\t<format>\t<platform>\t<gameId>\t<disc>\t<revision>\t<region>\t<size>\t<title>" or "error\t<reason>".
extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_rom_WiiDsDisc_nativeIdentify(JNIEnv* env, jclass, jintArray fds) {
    std::string result;
    try {
        auto parts = TakeParts(env, fds);
        const Info info = Identify(parts);
        result = "ok\t" + std::string(FormatName(info.format)) + "\t" +
                 (info.platform == Platform::Wii ? "Wii" : "GameCube") + "\t" + Field(info.gameId) + "\t" +
                 std::to_string(info.discNumber) + "\t" + std::to_string(info.revision) + "\t" +
                 Field(info.region) + "\t" + std::to_string(info.discSize) + "\t" + TitleField(info.title);
    } catch (const std::exception& e) {
        result = "error\t" + Field(e.what());
    } catch (...) {
        result = "error\tThe file couldn't be read.";
    }
    return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_rom_WiiDsDisc_nativeExtractWii(JNIEnv* env, jclass, jintArray fds, jstring destDir) {
    return Extract(env, fds, destDir, true);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_rom_WiiDsDisc_nativeExtractGameCube(JNIEnv* env, jclass, jintArray fds, jstring destIso) {
    return Extract(env, fds, destIso, false);
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_driftdroid_android_rom_WiiDsDisc_nativeProgress(JNIEnv* env, jclass) {
    const jlong values[2] = {jlong(g_progress.done.load()), jlong(g_progress.total.load())};
    jlongArray out = env->NewLongArray(2);
    env->SetLongArrayRegion(out, 0, 2, values);
    return out;
}

extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_rom_WiiDsDisc_nativeCancel(JNIEnv*, jclass) {
    g_progress.cancel = true;
}

extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_rom_WiiDsDisc_nativeResetCancel(JNIEnv*, jclass) {
    g_progress.cancel = false;
}
