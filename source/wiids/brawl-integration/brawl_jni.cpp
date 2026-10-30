#include <jni.h>
#include <cstdio>
#include <dlfcn.h>
#include <android/log.h>

// Bind the existing, tested engine to the relocated Java class. No engine binary rewriting.
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm,void*) {
    JNIEnv* env=nullptr;
    if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK)return JNI_ERR;
    void* engine=dlopen("libwiids_brawl.so",RTLD_NOW|RTLD_LOCAL);
    if(!engine){__android_log_print(ANDROID_LOG_ERROR,"WiiDS-Brawl","Engine load failed: %s",dlerror());return JNI_ERR;}
    struct Entry {const char* name;const char* signature;};
    static const Entry entries[]={
        {"nativeSetTouchState","(IZF)V"},
        {"nativeReadBrawlState","()[I"},
        {"nativeFlushSaves","()Z"},
        {"nativeCommitGameSave","()Z"},
        {"nativeSetLowerPanels","(Z)V"},
        {"nativeSceneName","()Ljava/lang/String;"},
        {"nativeSetPaused","(Z)V"},
        {"nativeSetPadButtons","(I)V"},
        {"nativeSetControls","(ZZZ)V"},
        {"nativeApplyDisplaySettings","(IFZ)V"},
        {"nativeGraphicsInfo","()Ljava/lang/String;"},
        {"nativeSetDeviceName","(Ljava/lang/String;)V"},
        {"nativeSessionActive","()Z"},
        {"nativePrepareToLeave","()V"},
        {"nativeTap","(FF)V"},
        {"nativeTapScreen","()Z"},
        {"nativeTakeSettings","()[F"}
    };
    jclass cls=env->FindClass("dev/wiids/brawl/GameActivity");if(!cls)return JNI_ERR;
    JNINativeMethod methods[sizeof(entries)/sizeof(entries[0])];
    for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);i++) {
        char symbol[160];snprintf(symbol,sizeof(symbol),"Java_com_wither_nsmbw_GameActivity_%s",entries[i].name);
        void* function=dlsym(engine,symbol);
        if(!function){__android_log_print(ANDROID_LOG_ERROR,"WiiDS-Brawl","Missing engine bridge: %s",symbol);return JNI_ERR;}
        methods[i]={const_cast<char*>(entries[i].name),const_cast<char*>(entries[i].signature),function};
    }
    if(env->RegisterNatives(cls,methods,sizeof(methods)/sizeof(methods[0]))<0)return JNI_ERR;
    __android_log_print(ANDROID_LOG_INFO,"WiiDS-Brawl","Registered all 17 Brawl methods in the integrated process");
    env->DeleteLocalRef(cls);return JNI_VERSION_1_6;
}
