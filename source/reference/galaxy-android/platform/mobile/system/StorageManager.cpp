#include "StorageManager.h"

#include <android/log.h>
#include <dirent.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vector>

#include "port/port.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyStorage", __VA_ARGS__)

namespace mobile_system {

bool StorageManager::isDir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool StorageManager::checkGameFolder(const std::string& dir, bool* ready) {
    struct stat st;
    if (stat((dir + "/sys/fst.bin").c_str(), &st) != 0 || !S_ISREG(st.st_mode) || !isDir(dir + "/files")) {
        return false;
    }
    char magic[4] = {};
    FILE* f = fopen((dir + "/files/ObjectData/Coin.arc").c_str(), "rb");
    bool converted = f && fread(magic, 1, 4, f) == 4 && memcmp(magic, "CRAR", 4) == 0;
    if (f) fclose(f);

    bool dolData = true;
    for (const char* name : {"ErrorMessageArchive.arc", "StoryEvent.bcsv", "GalaxyID.bcsv"}) {
        dolData = dolData && stat((dir + "/sys/" + name).c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }
    bool known = port_dvd_identify(dir.c_str()) != nullptr;
    if (ready) *ready = converted && dolData && known;
    return true;
}

std::string StorageManager::findGameData(const std::string& extDir) {
    // Only this runtime's own converted disc may be selected automatically.
    const std::string root = extDir + "/GalaxyData";
    bool ready = false;
    return checkGameFolder(root, &ready) && ready ? root : "";
}

void StorageManager::promptStoragePermissionIfMissing(ANativeActivity* activity) {
    if (!activity || !activity->vm || !activity->clazz) return;
    JNIEnv* env = nullptr;
    if (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;

    if (env->ExceptionCheck()) env->ExceptionClear();

    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    if (!versionClass) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }
    jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
    if (!sdkIntField) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(versionClass);
        return;
    }
    jint sdkInt = env->GetStaticIntField(versionClass, sdkIntField);
    env->DeleteLocalRef(versionClass);

    if (sdkInt >= 30) {
        jclass envClass = env->FindClass("android/os/Environment");
        if (!envClass) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            return;
        }
        jmethodID isManagerMethod = env->GetStaticMethodID(envClass, "isExternalStorageManager", "()Z");
        if (!isManagerMethod) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(envClass);
            return;
        }
        jboolean isManager = env->CallStaticBooleanMethod(envClass, isManagerMethod);
        env->DeleteLocalRef(envClass);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }

        if (!isManager) {
            LOGI("MANAGE_EXTERNAL_STORAGE permission missing, launching settings prompt...");
            jclass intentClass = env->FindClass("android/content/Intent");
            if (!intentClass) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                return;
            }
            jmethodID intentCtor = env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");

            jclass uriClass = env->FindClass("android/net/Uri");
            if (!uriClass || !intentCtor) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                if (intentClass) env->DeleteLocalRef(intentClass);
                if (uriClass) env->DeleteLocalRef(uriClass);
                return;
            }
            jmethodID parseMethod = env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");

            jclass actClass = env->GetObjectClass(activity->clazz);
            if (!actClass || !parseMethod) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(intentClass);
                env->DeleteLocalRef(uriClass);
                if (actClass) env->DeleteLocalRef(actClass);
                return;
            }
            jmethodID getPackageNameMethod = env->GetMethodID(actClass, "getPackageName", "()Ljava/lang/String;");
            jmethodID startActivityMethod = env->GetMethodID(actClass, "startActivity", "(Landroid/content/Intent;)V");

            if (getPackageNameMethod && startActivityMethod) {
                jstring pkgName = (jstring)env->CallObjectMethod(activity->clazz, getPackageNameMethod);
                if (pkgName) {
                    const char* pkgChars = env->GetStringUTFChars(pkgName, nullptr);
                    std::string uriStr = std::string("package:") + (pkgChars ? pkgChars : "");
                    if (pkgChars) env->ReleaseStringUTFChars(pkgName, pkgChars);

                    jstring jUriStr = env->NewStringUTF(uriStr.c_str());
                    jobject jUri = env->CallStaticObjectMethod(uriClass, parseMethod, jUriStr);

                    jstring actionStr = env->NewStringUTF("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION");
                    jobject intent = env->NewObject(intentClass, intentCtor, actionStr, jUri);

                    if (intent) {
                        env->CallVoidMethod(activity->clazz, startActivityMethod, intent);
                        env->DeleteLocalRef(intent);
                    }
                    if (actionStr) env->DeleteLocalRef(actionStr);
                    if (jUri) env->DeleteLocalRef(jUri);
                    if (jUriStr) env->DeleteLocalRef(jUriStr);
                    env->DeleteLocalRef(pkgName);
                }
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(actClass);
            env->DeleteLocalRef(uriClass);
            env->DeleteLocalRef(intentClass);
        }
    }
}

}  // namespace mobile_system
