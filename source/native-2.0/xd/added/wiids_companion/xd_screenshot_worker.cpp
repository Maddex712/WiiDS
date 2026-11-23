// Windows verification host only. No capture, encoding, clock changes or I/O on
// the native game thread; a full queue drops a screenshot request.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "xd_spsc.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <chrono>
extern "C" void* PortXD_CaptureWindow(void*,int*,int*);
extern "C" bool PortXD_SavePng(void*,const wchar_t*);
namespace {
struct Job {void* window;char folder[768];uint32_t field,picture;};
struct Worker {
    XdSpsc<Job,8> queue;
    Worker(){std::thread([this]{
        for(;;){Job job{};if(!queue.pop(job)){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
            std::filesystem::create_directories(job.folder);
            int width=0,height=0;void* bitmap=PortXD_CaptureWindow(job.window,&width,&height);
            auto png=std::filesystem::path(job.folder)/("f"+std::to_string(job.picture)+".png");
            bool saved=PortXD_SavePng(bitmap,png.wstring().c_str());
            auto meta=png;meta.replace_extension(".txt");
            if(FILE* file=std::fopen(meta.string().c_str(),"w")){
                std::fprintf(file,"requested field %u\nrequested picture %u\nwindow %dx%d\ncapture_worker 1\nsaved %d\n",job.field,job.picture,width,height,saved);std::fclose(file);
            }
        }
    }).detach();}
};
// Process-lifetime state: no detached thread touches destructed static objects.
Worker& worker(){static Worker* value=new Worker;return *value;}
}
extern "C" void PortXD_ScreenshotTo(void* window,const char* folder,uint32_t field,uint32_t picture){
    if(!window||!folder||std::strlen(folder)>=sizeof(Job::folder))return;
    Job job{};job.window=window;job.field=field;job.picture=picture;std::strcpy(job.folder,folder);worker().queue.push(job);
}
extern "C" void PortXD_Screenshot(void* window,uint32_t field,uint32_t picture){
    const char* folder=std::getenv("XD_SHOT_DIR");if(folder)PortXD_ScreenshotTo(window,folder,field,picture);
}
