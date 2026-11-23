#include <SDL3/SDL.h>
#include <android/dlext.h>
#include <android/log.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <jni.h>
#include "xd_companion.h"
#include "xd_surface_state.h"
static std::atomic<void*> loadedGame{nullptr};
extern "C" JNIEXPORT jboolean JNICALL Java_dev_wiids_xd_XdNative_menuOpen(JNIEnv*,jclass,jboolean open) {
    auto game=loadedGame.load(std::memory_order_acquire);
    auto fn=game?reinterpret_cast<int(*)(int)>(dlsym(game,"WiidsXD_MenuOpen")):nullptr;
    return fn&&fn(open==JNI_TRUE)?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_wiids_xd_XdNative_runtimeSet(JNIEnv*,jclass,jint key,jint value) {
    auto game=loadedGame.load(std::memory_order_acquire);
    auto fn=game?reinterpret_cast<int(*)(int,int)>(dlsym(game,"WiidsXD_RuntimeSet")):nullptr;
    return fn&&fn(key,value)?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT jintArray JNICALL Java_dev_wiids_xd_XdNative_runtimeRead(JNIEnv* env,jclass) {
    auto game=loadedGame.load(std::memory_order_acquire);
    auto fn=game?reinterpret_cast<int(*)(int*,int)>(dlsym(game,"WiidsXD_RuntimeRead")):nullptr;
    jint values[12]={};if(!fn||!fn(values,12))return nullptr;
    auto result=env->NewIntArray(12);if(result)env->SetIntArrayRegion(result,0,12,values);return result;
}
static void updateSurface(void* game,bool ready) {
    using Ready=void(*)(JNIEnv*,jclass,jboolean);
    auto callback=game?reinterpret_cast<Ready>(dlsym(game,"Java_dev_encounter_aurora_AuroraSurface_nativeSetSurfaceReady")):nullptr;
    if(callback) callback(nullptr,nullptr,ready?JNI_TRUE:JNI_FALSE);
}
static XdSurfaceState surfaceState(updateSurface);
extern "C" JNIEXPORT void JNICALL Java_dev_wiids_xd_XdSurface_nativeSetReady(JNIEnv*,jclass,jboolean ready) {
    surfaceState.setReady(ready==JNI_TRUE);
}
// JNI in this high-address host forwards to the one transport inside the low game image.
extern "C" int WiidsXD_Read(XdSnapshot* out) {
    using Read=int(*)(XdSnapshot*);
    void* game=loadedGame.load(std::memory_order_acquire);
    auto read=game?reinterpret_cast<Read>(dlsym(game,"WiidsXD_Read")):nullptr;
    if(read) return read(out);
    std::memset(out,0,sizeof(*out)); return 0;
}
extern "C" int WiidsXD_Submit(uint64_t epoch,int action,int arg) {
    using Submit=int(*)(uint64_t,int,int);
    void* game=loadedGame.load(std::memory_order_acquire);
    auto submit=game?reinterpret_cast<Submit>(dlsym(game,"WiidsXD_Submit")):nullptr;
    return submit?submit(epoch,action,arg):0;
}
extern "C" void WiidsXD_Suspend() {
    using Suspend=void(*)();
    void* game=loadedGame.load(std::memory_order_acquire);
    auto suspend=game?reinterpret_cast<Suspend>(dlsym(game,"WiidsXD_Suspend")):nullptr;
    if(suspend) suspend();
}
extern "C" __attribute__((visibility("default"))) int SDL_main(int argc,char** argv) {
    std::string data,libs,disc;
    for(int i=1;i+1<argc;i+=2) {
        if(!std::strcmp(argv[i],"--data")) data=argv[i+1];
        else if(!std::strcmp(argv[i],"--libraries")) libs=argv[i+1];
        else if(!std::strcmp(argv[i],"--disc")) disc=argv[i+1];
    }
    if(data.empty()||libs.empty()||disc.empty()||chdir(data.c_str())) return 1;
    // Exclusive app profile. Imported files and saves are siblings, never install outputs.
    setenv("XD_WIIDS_PROFILE",data.c_str(),1);
    setenv("XD_CARD_DIR",(data+"/saves").c_str(),1);
    setenv("XD_CACHE_DIR",(data+"/cache").c_str(),1);
    setenv("XD_RECORD","0",1);
    const size_t bytes=size_t(256)<<20;
    void* region=MAP_FAILED;
    for(uintptr_t at=0x10000000;at+bytes<=0x40000000;at+=bytes) {
        void* p=mmap(reinterpret_cast<void*>(at),bytes,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(p==reinterpret_cast<void*>(at)) { region=p; break; }
        if(p!=MAP_FAILED) munmap(p,bytes);
    }
    if(region==MAP_FAILED) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Pokemon XD","Cannot reserve the native game's address range.",nullptr); return 1; }
    android_dlextinfo ext{}; ext.flags=ANDROID_DLEXT_RESERVED_ADDRESS; ext.reserved_addr=region; ext.reserved_size=bytes;
    void* game=android_dlopen_ext((libs+"/libxd.so").c_str(),RTLD_NOW|RTLD_LOCAL,&ext);
    if(!game) { const char* e=dlerror(); __android_log_print(ANDROID_LOG_ERROR,"WiiDS-XD","%s",e?e:"dlopen failed"); SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Pokemon XD",e?e:"Native runtime loading failed.",nullptr); munmap(region,bytes); return 1; }
    using Entry=int(*)(int,char**);
    auto entry=reinterpret_cast<Entry>(dlsym(game,"aurora_main"));
    if(!entry) return 1;
    loadedGame.store(game,std::memory_order_release);
    surfaceState.setGame(game);
    char name[]="pokemon-xd";
    char* args[]={name,disc.data(),nullptr};
    int result=entry(2,args);
    WiidsXD_Suspend();surfaceState.setGame(nullptr);loadedGame.store(nullptr,std::memory_order_release);
    return result;
}
