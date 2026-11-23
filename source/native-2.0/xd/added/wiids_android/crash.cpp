#include "crash_report.h"
#include "port_seh.h"
#include "xd_backtrace.h"
#include <android/log.h>
#include <dlfcn.h>
#include <signal.h>
#include <cstdio>
#include <unistd.h>
#include <mutex>
static thread_local PortSehFrame* top;
static PortCrashReportHook reportHook;
PortSehFrame::PortSehFrame():previous(top) { top=this; }
PortSehFrame::~PortSehFrame() { top=previous; }
extern "C" PortSehFrame* PortSeh_Top() { return top; }
static struct sigaction previousSegv{},previousBus{};
static std::once_flag installed;
static void fault(int sig,siginfo_t* info,void* context) {
    // Preserve the existing explicitly guarded optional-report reads only.
    if(top) siglongjmp(top->env,1);
    const auto& previous=sig==SIGBUS?previousBus:previousSegv;
    if(previous.sa_handler==SIG_IGN)return;
    if(previous.sa_handler!=SIG_DFL&&previous.sa_handler!=nullptr) {
        if(previous.sa_flags&SA_SIGINFO)previous.sa_sigaction(sig,info,context);
        else previous.sa_handler(sig);
        return;
    }
    // Preserve ART/libsigchain and debuggerd handlers. Only a genuinely
    // unhandled fault restores the previous default disposition.
    sigaction(sig,&previous,nullptr);raise(sig);
}
extern "C" void PortCrash_Install() {
    std::call_once(installed,[]{
        struct sigaction sa{}; sa.sa_sigaction=fault; sa.sa_flags=SA_SIGINFO|SA_ONSTACK;
        sigemptyset(&sa.sa_mask); sigaction(SIGSEGV,&sa,&previousSegv);sigaction(SIGBUS,&sa,&previousBus);
    });
}
extern "C" void PortCrash_DescribeAddress(const void* address,char* out,size_t size) {
    Dl_info info{}; dladdr(address,&info);
    std::snprintf(out,size,"%p %s+%zx",address,info.dli_sname?info.dli_sname:"?",
      reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(info.dli_saddr));
}
extern "C" void PortCrash_PrintThreadStack(void*) {
    void* frames[64]; int count=backtrace(frames,64);
    for(int i=0;i<count;++i) { char line[512]; PortCrash_DescribeAddress(frames[i],line,sizeof line); __android_log_print(ANDROID_LOG_ERROR,"WiiDS-XD","%s",line); }
}
extern "C" void PortCrash_SetReportHook(PortCrashReportHook hook) { reportHook=hook; }
extern "C" int PortCrash_RunReportHook(const char* kind,const char* summary,void* pointers,unsigned) {
    if(!reportHook) return 0;
    PortCrashReport report{kind,summary,pointers,static_cast<unsigned long>(gettid())}; reportHook(&report); return 1;
}
extern "C" int PortCrash_WriteMinidump(const wchar_t*,const PortCrashReport*) { return 0; } // Android tombstones replace Windows dumps.
extern "C" void PortCrash_WatchWrites(uintptr_t) { __android_log_print(ANDROID_LOG_WARN,"WiiDS-XD","Desktop hardware watchpoints unavailable on Android"); }
extern "C" void PortCrash_SampleThread(unsigned,unsigned,unsigned) { __android_log_print(ANDROID_LOG_WARN,"WiiDS-XD","Desktop sampling unavailable on Android"); }
