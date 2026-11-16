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
#include <android/native_window_jni.h>
#include <mutex>
#include "xd_presentation.h"
#include "xd_companion.h"
#include "xd_surface_state.h"
#include "xd_address_reservation.h"
#include "early_plan.h"
extern "C" const XdEarlyMemory* WiidsXD_EarlyMemory();
static std::atomic<void*> loadedGame{nullptr};
namespace {
std::mutex lowerMutex;ANativeWindow* lowerWindow=nullptr;int lowerWidth=0,lowerHeight=0;
void forwardLower(void* game){
    auto fn=game?reinterpret_cast<void(*)(void*,int,int)>(dlsym(game,"WiidsXD_LowerWindow")):nullptr;
    if(fn)fn(lowerWindow,lowerWidth,lowerHeight);
}
}
extern "C" JNIEXPORT void JNICALL Java_dev_wiids_xd_XdNative_lowerSurface(JNIEnv* env,jclass,jobject surface,jint width,jint height){
    auto* window=surface?ANativeWindow_fromSurface(env,surface):nullptr;
    std::lock_guard hold(lowerMutex);
    if(lowerWindow)ANativeWindow_release(lowerWindow);
    lowerWindow=window;lowerWidth=width;lowerHeight=height;
    forwardLower(loadedGame.load(std::memory_order_acquire));
}
extern "C" int WiidsXD_PresentRead(XdPresentationState* out){
    auto game=loadedGame.load(std::memory_order_acquire);
    auto fn=game?reinterpret_cast<int(*)(XdPresentationState*)>(dlsym(game,"WiidsXD_PresentRead")):nullptr;
    if(fn)return fn(out);std::memset(out,0,offsetof(XdPresentationState,tab));return 0;
}
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
    const std::string image=libs+"/libxd.so";
    const XdEarlyMemory* early=WiidsXD_EarlyMemory();
    if(!early || !early->ready) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Pokemon XD","Address space is occupied. See XD-memory log for mapping owners. Close and relaunch Pokemon XD; no memory was overwritten.",nullptr); return 1; }
    size_t bytes=0;
    void* region=xd_address::reserveImage(image.c_str(),bytes);
    if(region==MAP_FAILED) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Pokemon XD","Cannot reserve the native game's address range.",nullptr); return 1; }
    __android_log_print(ANDROID_LOG_INFO,"WiiDS-XD","Reserved game ELF at %p, %zu bytes (free maps gap)",region,bytes);
    android_dlextinfo ext{}; ext.flags=ANDROID_DLEXT_RESERVED_ADDRESS; ext.reserved_addr=region; ext.reserved_size=bytes;
    void* game=android_dlopen_ext(image.c_str(),RTLD_NOW|RTLD_LOCAL,&ext);
    if(!game) { const char* e=dlerror(); __android_log_print(ANDROID_LOG_ERROR,"WiiDS-XD","%s",e?e:"dlopen failed"); SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Pokemon XD",e?e:"Native runtime loading failed.",nullptr); munmap(region,bytes); return 1; }
    auto adopt=reinterpret_cast<int(*)(const XdEarlyMemory*)>(dlsym(game,"PortMem_AdoptEarly"));
    if(!adopt || !adopt(early)) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Pokemon XD","Native memory reservation handoff failed safely.",nullptr); return 1; }
    using Entry=int(*)(int,char**);
    auto entry=reinterpret_cast<Entry>(dlsym(game,"aurora_main"));
    if(!entry) return 1;
    loadedGame.store(game,std::memory_order_release);
    {std::lock_guard hold(lowerMutex);forwardLower(game);}
    surfaceState.setGame(game);
    char name[]="pokemon-xd";
    char* args[]={name,disc.data(),nullptr};
    int result=entry(2,args);
    WiidsXD_Suspend();surfaceState.setGame(nullptr);loadedGame.store(nullptr,std::memory_order_release);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL Java_dev_wiids_xd_XdNative_lowerTouch(JNIEnv*,jclass,jfloat x,jfloat y){
    auto game=loadedGame.load(std::memory_order_acquire);auto fn=game?reinterpret_cast<int(*)(float,float)>(dlsym(game,"WiidsXD_LowerTouch")):nullptr;
    return fn&&fn(x,y)?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT jint JNICALL Java_dev_wiids_xd_XdNative_fieldColor(JNIEnv*,jclass){
    auto game=loadedGame.load(std::memory_order_acquire);auto fn=game?reinterpret_cast<int(*)()>(dlsym(game,"WiidsXD_FieldColor")):nullptr;return fn?fn():0xA0A0A0;
}
