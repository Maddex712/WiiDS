#include "port/profile.h"
#include "revolution/nand.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fstream>
#include <vector>
extern "C" void port_host_alloc_scope_enter() {}
extern "C" void port_host_alloc_scope_leave() {}
extern "C" void port_log(const char*, ...) {}
extern "C" __attribute__((noreturn)) void port_fatal(const char* f,...) {
    va_list a;va_start(a,f);vfprintf(stderr,f,a);va_end(a);abort();
}
extern "C" void port_nand_set_root(const char*);
static void dirs(const std::string& p) { for(size_t i=1;i<p.size();i++)if(p[i]=='/')mkdir(p.substr(0,i).c_str(),0700);mkdir(p.c_str(),0700); }
static std::vector<char> read(const std::string& p) {std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static void save(const std::string& internal,const std::string& external,char marker) {
    dirs(internal);dirs(external);
    port_profile_configure(internal.c_str(),external.c_str());
    const std::string nand=internal+"/nand";dirs(nand);port_nand_set_root(nand.c_str());
    assert(NANDCreate("GameData.bin",0,0)==0);
    NANDFileInfo info;assert(NANDOpen("GameData.bin",&info,NAND_ACCESS_WRITE)==0);
    char data[1024];memset(data,marker,sizeof(data));
    assert(NANDWrite(&info,data,sizeof(data))==sizeof(data));assert(NANDClose(&info)==0);
    assert(read(external+"/GalaxyData/saves/GameData.bin")==std::vector<char>(1024,marker));
    assert(port_profile_data_path("/saves/quick")==external+"/GalaxyData/saves/quick");
    assert(port_profile_internal_path("/vk_pipeline_cache.bin")==internal+"/vk_pipeline_cache.bin");
}
int main(int argc,char**argv) {
    assert(argc==2);const std::string base=argv[1];
    const std::string oi=base+"/private/galaxy",oe=base+"/external/galaxy";
    const std::string ki=base+"/private/galaxy-kaizo",ke=base+"/external/galaxy-kaizo";
    save(oi,oe,'O');auto original=read(oe+"/GalaxyData/saves/GameData.bin");
    assert(port_profile_exchange_path("/GameData.bin")=="/sdcard/Download/GameData.bin");
    save(ki,ke,'K');
    assert(read(oe+"/GalaxyData/saves/GameData.bin")==original);
    assert(read(oi+"/nand/title/00010000/524d4750/data/GameData.bin")==original);
    assert(port_profile_exchange_path("/GameData.bin")=="/sdcard/Download/WiiDS-Kaizo/GameData.bin");
    // Selecting original again must not read or modify the mod mirror/cache root.
    port_profile_configure(oi.c_str(),oe.c_str());
    assert(port_profile_data_path("/saves/GameData.bin")==oe+"/GalaxyData/saves/GameData.bin");
    assert(read(ke+"/GalaxyData/saves/GameData.bin")==std::vector<char>(1024,'K'));
    puts("PASS actual profile paths + NAND create/write/close: distinct mirrors and NAND; original bytes preserved; cache and exchange paths isolated");
}
