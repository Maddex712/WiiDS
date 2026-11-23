// Windows host experiment: schedule completed frames at a fixed phase of the
// existing 60 Hz retrace clock. Does not change game time, tick count or draws.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <cstdlib>
#include <thread>
extern "C" unsigned VIGetRetraceCount();
extern "C" unsigned PortVI_GetFlipCount();
extern "C" void WiidsXD_HostPace(){
    static const bool enabled=std::getenv("XD_FRAME_PACER")!=nullptr;
    if(!enabled)return;
    using Clock=std::chrono::steady_clock;
    static unsigned baseField=0,lastField=0;static Clock::time_point base;
    // Retraces can batch after scheduler delays; completed pictures cannot.
    const unsigned field=PortVI_GetFlipCount();
    if(!baseField){baseField=field;base=Clock::now()+std::chrono::milliseconds(3);}
    if(field==lastField)return;lastField=field;
    auto due=base+std::chrono::nanoseconds((long long)(field-baseField)*1000000000LL/60);
    auto now=Clock::now();
    // A paused game or loading gap establishes a fresh phase, without catchup.
    if(now-due>std::chrono::milliseconds(100)){baseField=field;base=now;due=now;}
    if(due-now>std::chrono::microseconds(700))std::this_thread::sleep_until(due-std::chrono::microseconds(700));
    while(Clock::now()<due)YieldProcessor();
}
