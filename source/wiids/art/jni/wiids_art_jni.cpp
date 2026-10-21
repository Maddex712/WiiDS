// JNI surface of wiids_art (com.driftdroid.android.rom.WiiDsArt). Builds a game's companion art
// from the player's imported disc into the folder the game reads it from.
#include "wiids_art_profiles.h"

#include <jni.h>

#include <exception>
#include <string>

namespace {

std::string Take(JNIEnv* env, jstring value) {
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string out = chars;
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

// Messages can quote names read from a disc or pack; NewStringUTF needs valid Modified UTF-8.
std::string Printable(const char* s) {
    std::string out = s;
    for (char& c : out)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) c = '?';
    return out;
}

template <typename Fn>
jstring Run(JNIEnv* env, Fn&& fn) {
    try {
        fn();
        return nullptr;
    } catch (const std::exception& e) {
        return env->NewStringUTF(Printable(e.what()).c_str());
    } catch (...) {
        return env->NewStringUTF("The game's art couldn't be built.");
    }
}

} // namespace

// kind: "nsmb-hud", "sms-hud", "mkw-art", "mkw-audio", "mkw-cases", "nsmb-cases", "sms-cases".
extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_rom_WiiDsArt_nativeBuild(JNIEnv* env, jclass, jstring kind, jstring source, jstring out) {
    const std::string k = Take(env, kind), in = Take(env, source), dest = Take(env, out);
    return Run(env, [&] {
        using namespace wiids::art;
        if (k == "nsmb-hud") BuildNsmbHud(in, dest);
        else if (k == "sms-hud") BuildSunshineHud(in, dest);
        else if (k == "mkw-art") BuildMarioKartArt(in, dest);
        else if (k == "mkw-audio") BuildMarioKartAudio(in, dest);
        else if (k == "mkw-cases") BuildMarioKartCases(in, dest);
        else if (k == "nsmb-cases") BuildNsmbCases(in, dest);
        else if (k == "sms-cases") BuildSunshineCases(in, dest);
        else throw std::runtime_error("Unknown art set " + k);
    });
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_rom_WiiDsArt_nativeBuildMaps(JNIEnv* env, jclass, jstring discRoot, jstring out) {
    const std::string disc = Take(env, discRoot), dest = Take(env, out);
    return Run(env, [&] { wiids::art::BuildMarioKartMaps(disc, dest); });
}

// Retro Rewind maps for the installed pack; incremental (only changed tracks are rebaked).
extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_rom_WiiDsArt_nativeBuildRetroMaps(JNIEnv* env, jclass, jstring pack, jstring out) {
    const std::string root = Take(env, pack), dest = Take(env, out);
    return Run(env, [&] { wiids::art::BuildRetroRewindMaps(root, dest); });
}
