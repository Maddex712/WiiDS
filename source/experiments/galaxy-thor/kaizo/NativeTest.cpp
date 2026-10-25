#include "port/kaizo.h"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/System/GalaxyCometScheduler.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <vector>

extern "C" void port_log(const char*, ...) {}
extern "C" __attribute__((noreturn)) void port_fatal(const char* fmt, ...) {
    va_list args;va_start(args,fmt);vfprintf(stderr,fmt,args);va_end(args);abort();
}
int main(int argc,char**argv) {
    assert(argc==3);
    port_kaizo_configure("/test/galaxy", "/test/galaxy");
    assert(!port_kaizo_enabled());
    int count=GameEventFlagTable::getTableSize();
    auto flags=const_cast<GameEventFlag*>(GameEventFlagTable::getFlag(0));
    std::vector<GameEventFlag> before(flags,flags+count);
    port_kaizo_apply_flags(flags,count);
    assert(!memcmp(flags,before.data(),count*sizeof(GameEventFlag)));
    GalaxyCometTimePaper stock[]={{"base",0},{nullptr,0}};
    assert(port_kaizo_comet_table(1,stock)==stock);
    port_kaizo_configure(argv[1],argv[2]);
    assert(port_kaizo_enabled());
    port_kaizo_apply_flags(flags,count);
    int changed=0;
    for(int i=0;i<count;i++) {
        if (memcmp(&flags[i],&before[i],sizeof(GameEventFlag))) {
            assert(i>=121&&i<145);changed++;
        }
    }
    assert(changed==24);
    assert(!strcmp(flags[121].mName,"ExOpenSubTransformationExGalaxy"));
    assert(flags[121].mStarID==3);
    assert(!strcmp(flags[121].mGalaxyName,"StarDustGalaxy"));
    assert(!strcmp(flags[137].mName,"AppearTransformationExGalaxy"));
    for(int dome=0;dome<6;dome++) {
        auto table=port_kaizo_comet_table(dome,stock);
        if(dome==0||dome==5)assert(table==stock);
        else {assert(table!=stock);int n=0;while(table[n].mGalaxyName){assert(table[n].mCategory<=1);assert(++n<64);}assert(n>0);}
    }
    // Constructor recreation must not reapply changed indices or invalidate names.
    port_kaizo_apply_flags(flags,count);
    assert(!strcmp(flags[121].mName,"ExOpenSubTransformationExGalaxy"));
    printf("PASS actual native loader + actual GameEventFlag table: vanilla unchanged; 24 exact scoped overrides; four terminated comet schedules; repeat application safe\n");
}
