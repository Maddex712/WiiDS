#include "hle/disc_interface.h"
#include "memory.h"
#include "runtime_config.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr uint32_t base = 0x80000000, in = base + 64, out = base + 1024;
std::array<uint8_t,4096> ram;
std::filesystem::path root;
int checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL %s\n",message); std::exit(1); }
}
void Sentinel() { ram.fill(0xC7); }
bool Unchanged() { return std::all_of(ram.begin(),ram.end(),[](uint8_t v){return v==0xC7;}); }
void Write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path,std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if (!file.good()) throw std::runtime_error("fixture write failed");
}
int32_t Request(uint32_t fd, uint32_t cmd=0xDA, uint32_t input=in,
                uint32_t inputLength=32, uint32_t output=out, uint32_t outputLength=96) {
    return DiscInterface::Ioctl(fd,cmd,input,inputLength,output,outputLength);
}
void ErrorWithoutWrite(bool result,const char* message) {
    Check(result,message); Check(Unchanged(),"failed request leaves all guest RAM unchanged");
}
}

bool Memory::Contains(uint32_t a,size_t n) {
    return a>=base && static_cast<uint64_t>(a-base)+n<=ram.size();
}
uint8_t* Memory::GetPointer(uint32_t a) { return GetPointer(a,1); }
uint8_t* Memory::GetPointer(uint32_t a,size_t n) {
    if(!Contains(a,n)) throw std::runtime_error("invalid mock guest range");
    return ram.data()+(a-base);
}
std::filesystem::path RuntimeConfigFile::ResolvedDvdRoot() { return root; }

int main(int argc,char**argv) {
    if(argc!=2) return 2;
    root=argv[1];
    // Only this test's isolated temporary folder is written. These arbitrary
    // bytes are a file-I/O fixture, never valid NSMB verification metadata.
    std::filesystem::create_directories(root/"disc");
    std::filesystem::create_directories(root/"sys");
    std::vector<uint8_t> fixture(64), boot(108);
    for(size_t i=0;i<fixture.size();++i) fixture[i]=static_cast<uint8_t>((i*37+19)^0x5A);
    for(size_t i=0;i<boot.size();++i) boot[i]=static_cast<uint8_t>(i*11+7);
    Check(DiscInterface::Open(2)<0,"write-only open refused");
    Check(DiscInterface::Open(3)<0,"read/write open refused");
    Check(!DiscInterface::OwnsFd(123),"foreign descriptor unowned");
    const auto fd=static_cast<uint32_t>(DiscInterface::Open(1));
    Check(DiscInterface::OwnsFd(fd),"read-only open owns descriptor");
    const auto savedRoot=root; root.clear();
    Sentinel(); ErrorWithoutWrite(Request(fd)==-6,"unset root reports missing metadata");
    root=savedRoot;
    Sentinel(); ErrorWithoutWrite(Request(fd)<0,"missing sidecar fails");
    std::filesystem::create_directory(root/"disc/bca.bin");
    Sentinel(); ErrorWithoutWrite(Request(fd)==-12,"nonregular sidecar fails with I/O error");
    std::filesystem::remove(root/"disc/bca.bin");
    Write(root/"disc/bca.bin",std::vector<uint8_t>(63,0xAB));
    Sentinel(); ErrorWithoutWrite(Request(fd)<0,"truncated sidecar fails");
    Write(root/"disc/bca.bin",std::vector<uint8_t>(65,0xAB));
    Sentinel(); ErrorWithoutWrite(Request(fd)<0,"oversized sidecar fails");
    Write(root/"disc/bca.bin",fixture);
    Sentinel(); Check(Request(fd)==1,"exact-size arbitrary sidecar read succeeds");
    Check(std::equal(fixture.begin(),fixture.end(),ram.begin()+1024),"sidecar bytes copied verbatim");
    Check(std::all_of(ram.begin(),ram.begin()+1024,[](uint8_t v){return v==0xC7;}) &&
          std::all_of(ram.begin()+1088,ram.end(),[](uint8_t v){return v==0xC7;}),"only payload changes; output tail preserved");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDE)<0,"unknown command fails");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDA,0,32)<0,"null input fails");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDA,in,31)<0,"short input fails");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDA,in,33)<0,"oversized mapped command buffer fails");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDA,0xFFFFFFF0,32)<0,"overflowing input range fails");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDA,base+4080,32)<0,"partly unmapped input fails");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDA,base+4064,64)<0,"unmapped supplied input tail fails");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDA,in,32,0,96)<0,"null output fails");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDA,in,32,out,63)<0,"short output fails");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDA,in,32,0xFFFFFFF0,96)<0,"overflowing output range fails");
    Sentinel(); ErrorWithoutWrite(Request(fd,0xDA,in,32,base+4032,96)<0,"unmapped output tail fails before write");
    Sentinel(); ErrorWithoutWrite(Request(123)<0,"foreign descriptor cannot read metadata");
    Sentinel(); ErrorWithoutWrite(Request(fd,0x70,in,32,out,64)<0,"missing boot header fails");
    Write(root/"sys/boot.bin",std::vector<uint8_t>(31,0xEE));
    Sentinel(); ErrorWithoutWrite(Request(fd,0x70,in,32,out,64)<0,"short boot header fails");
    Write(root/"sys/boot.bin",boot);
    Sentinel(); Check(Request(fd,0x70,in,32,out,64)==1,"boot header read succeeds");
    Check(std::equal(boot.begin(),boot.begin()+32,ram.begin()+1024),"first32 boot bytes copied verbatim");
    Check(std::all_of(ram.begin()+1056,ram.end(),[](uint8_t v){return v==0xC7;}),"boot output tail preserved");
    Check(DiscInterface::Close(fd)==0,"owned descriptor closes");
    Check(DiscInterface::OwnsFd(fd),"closed descriptor remains routed to DI instead of generic fallback");
    Sentinel(); ErrorWithoutWrite(Request(fd)<0,"closed descriptor cannot read");
    Check(DiscInterface::Close(fd)<0,"double close fails");
    const auto defaultFd=static_cast<uint32_t>(DiscInterface::Open(0));
    Check(DiscInterface::OwnsFd(defaultFd),"default access mode opens read-only");
    Check(DiscInterface::Close(defaultFd)==0,"default access descriptor closes");
    std::printf("PASS %d actual DI source checks; arbitrary test metadata only, no game check or Activity\n",checks);
}
