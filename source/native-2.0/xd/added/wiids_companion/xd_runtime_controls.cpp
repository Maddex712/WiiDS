#include "xd_companion.h"
#include <settings.h>
#include <port_freeze.h>
#include <SDL3/SDL_timer.h>
#include <atomic>
#include <array>
extern "C" void GSgfxVideoSetLockFrameRate(int);
extern "C" void PortXD_SaveSettings();
extern "C" bool PortCARD_Busy();
extern "C" void PortVideo_ApplySettings();
extern "C" void PortAudio_SetVolume(float);
extern "C" void PortSfx_SetVolume(float);
extern "C" int PortXD_AudioCategoryVolumeChanged(const char*,int);
namespace {
namespace es=engine::settings;
std::atomic<bool> wanted{false},paused{false},initialized{false},logic60{true};
std::atomic<int> requestedLock{1};
std::atomic<void(*)()> pauseProbe{nullptr};
std::array<std::atomic<int>,12> values{},pending{};
constexpr const char* ids[]={nullptr,nullptr,"video.widescreen","wiids.logic_60","video.resolution_scale","video.fps_counter",
    "audio.master_volume","audio.music_volume","audio.sfx_volume","audio.cry_volume","gameplay.text_speed","gameplay.fast_battle_animations"};
void refresh(){
    auto& r=es::Global();values[0]=PortCARD_Busy()?0:1;values[1]=paused?2:wanted?1:0;
    for(int i=2;i<12;i++){auto n=r.Find(ids[i]);values[i]=n==es::kNoSetting?-1:r.ValueOf(n).i;}
    logic60.store(values[3]!=0);
}
}
extern "C" void WiidsXD_RegisterSettings(){
    auto& r=es::Global();
    r.Add(es::Definition::Bool("wiids.logic_60",es::Category::Video,"Native 60 Hz logic",
        "Runs native updates every video field; game time and movement use their native field delta.",true,true).Preference().Implemented());
    r.Add(es::Definition::Bool("wiids.defaults_set",es::Category::System,"WiiDS display initialized","",false,false).Preference().Implemented());
}
extern "C" void WiidsXD_RuntimeInit(){
    for(auto& v:pending)v=-1;
    auto& r=es::Global();
    if(!r.Bool("wiids.defaults_set").Get()){
        r.SetBool("video.widescreen",true);r.SetEnum("video.widescreen_shape","16_9");
        r.SetBool("video.widescreen_edge_menus",true);r.SetBool("wiids.defaults_set",true);
        PortXD_SaveSettings();
    }
    PortFreeze_Capture();refresh();initialized=true;
}
extern "C" int WiidsXD_FilterLockRate(int rate){requestedLock=rate;return logic60.load()&&rate>0?0:rate;}
extern "C" void WiidsXD_SetPauseProbe(void(*probe)()){pauseProbe=probe;}
extern "C" int WiidsXD_MenuOpen(int open){
    if(!initialized)return 0;
    if(open){WiidsXD_BlockInput(1);wanted=true;}
    else {wanted=false;if(!paused)WiidsXD_BlockInput(0);}
    return 1;
}
extern "C" int WiidsXD_RuntimeRead(int* out,int count){
    if(!out||count!=12||!initialized)return 0;
    for(int i=0;i<12;i++)out[i]=values[i].load();out[1]=paused?2:wanted?1:0;return 1;
}
extern "C" int WiidsXD_RuntimeSet(int key,int value){
    if(!paused||key<2||key>=12)return 0;
    int max=(key>=6&&key<=9)?100:key==4?7:key==10?3:1;
    int min=key==4?1:0;
    if(value<min||value>max||(key==4&&value>3&&value!=7))return 0;pending[key]=value;return 1;
}
extern "C" void WiidsXD_RuntimeFrame(void(*pump)()){
    if(!initialized||!wanted)return;
    static int waits=0;
    if(PortCARD_Busy()||!PortFreeze_Ready(&waits)||!PortFreeze_Begin())return;
    paused=true;WiidsXD_Suspend();refresh();
    while(wanted){
        if(auto probe=pauseProbe.load())probe();
        bool changed=false,videoChanged=false,audioChanged=false;auto& r=es::Global();
        for(int i=2;i<12;i++){
            int v=pending[i].exchange(-1);if(v<0)continue;
            auto n=r.Find(ids[i]);if(n==es::kNoSetting)continue;
            auto current=r.ValueOf(n);current.i=v;
            if(r.Set(n,current)){
                changed=true;
                if(i==2||i==4||i==5)videoChanged=true;
                if(i>=6&&i<=9)audioChanged=true;
                if(i>=7&&i<=9)PortXD_AudioCategoryVolumeChanged(ids[i],v);
            }
        }
        if(changed){
            bool previous=logic60;refresh();
            if(videoChanged)PortVideo_ApplySettings();
            if(audioChanged){
                float master=(float)values[6].load()/100.0f;
                PortAudio_SetVolume(master);PortSfx_SetVolume(master*(float)values[8].load()/100.0f);
            }
            if(previous!=logic60.load())GSgfxVideoSetLockFrameRate(requestedLock.load());
            PortXD_SaveSettings();
        }
        PortFreeze_DrawPicture();PortFreeze_Present(pump);SDL_Delay(1);
    }
    PortFreeze_End();paused=false;WiidsXD_BlockInput(0);refresh();
}
