#include "early_plan.h"
#ifdef XD_EARLY_TEST
#include "test_platform.h"
#else
#include <sys/mman.h>
#include <unistd.h>
#include <android/log.h>
#define XD_MAPS_PATH "/proc/self/maps"
#endif
#include <errno.h>
#include <stdio.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
static XdEarlyMemory state;
static void owners(uintptr_t at,size_t bytes) {
    FILE* f=fopen(XD_MAPS_PATH,"r");char line[2048];
    if(!f){__android_log_print(ANDROID_LOG_ERROR,"XD-memory","Cannot read maps errno=%d",errno);return;}
    while(fgets(line,sizeof(line),f)) {
        unsigned long long a,z;
        if(sscanf(line,"%llx-%llx",&a,&z)==2&&a<at+bytes&&z>at)
            __android_log_print(ANDROID_LOG_ERROR,"XD-memory","owner of [%lx,%lx): %s",(unsigned long)at,(unsigned long)(at+bytes),line);
    }
    fclose(f);
}
struct Backend {
    bool exact(uintptr_t at,size_t bytes,bool writable) {
        int prot=writable?PROT_READ|PROT_WRITE:PROT_NONE;
        void* p=mmap((void*)at,bytes,prot,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
        // Old kernels may reject NOREPLACE. A hint is also non-destructive;
        // exact address validation is mandatory in either case.
        if(p==MAP_FAILED&&errno==EINVAL)p=mmap((void*)at,bytes,prot,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(p==(void*)at)return true;
        int e=errno;if(p!=MAP_FAILED)munmap(p,bytes);
        __android_log_print(ANDROID_LOG_WARN,"XD-memory","Reservation failed at %lx bytes=%zu errno=%d",(unsigned long)at,bytes,e);
        owners(at,bytes);return false;
    }
    void release(uintptr_t at,size_t n){munmap((void*)at,n);}
    uintptr_t gap(uintptr_t start,uintptr_t end,size_t bytes) {
        // maps are sorted by the kernel. Re-snapshot after a concurrent
        // allocation wins a candidate. Never replace a mapping from maps.
        for(unsigned retry=0;retry<16;++retry) {
            FILE* f=fopen(XD_MAPS_PATH,"r");if(!f)return 0;
            uintptr_t cursor=start,choice=0;char line[2048];
            while(fgets(line,sizeof(line),f)) {
                unsigned long long a,z;if(sscanf(line,"%llx-%llx",&a,&z)!=2)continue;
                if(z<=cursor)continue;
                uintptr_t limit=a<end?(uintptr_t)a:end;
                if(cursor<limit&&bytes<=limit-cursor){choice=cursor;break;}
                if(a>=end)break;
                if(z>cursor)cursor=((uintptr_t)z+0xffff)&~uintptr_t(0xffff);
                if(cursor>=end)break;
            }
            bool ok=!ferror(f);fclose(f);if(!ok)return 0;
            if(!choice&&cursor<end&&bytes<=end-cursor)choice=cursor;
            if(!choice){owners(start,end-start);return 0;}
            if(exact(choice,bytes,false))return choice;
        }
        return 0;
    }
};
__attribute__((constructor)) static void reserveBeforeSDL() {
    Backend b;xdReserveEarly(state,b);
    __android_log_print(state.ready?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,"XD-memory",
        "Early reservation ready=%u MEM1=%lx cache=%lx low=%lx (before SDL/ELF); no mappings replaced",
        state.ready,(unsigned long)state.mem1,(unsigned long)state.cache,(unsigned long)state.low);
}
extern "C" __attribute__((visibility("default"))) const XdEarlyMemory* WiidsXD_EarlyMemory(){return &state;}
