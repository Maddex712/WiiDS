#include "../../wiids/disc/src/wiids_disc.cpp"
#include <cstdio>
using namespace wiids::disc;
static void put32(Bytes& b,size_t at,uint32_t v) { b[at]=v>>24;b[at+1]=v>>16;b[at+2]=v>>8;b[at+3]=v; }
class FixtureBlob : public Blob {
public:
    Bytes data=Bytes(0x60000);
    bool decrypted=false;
    FixtureBlob() {
        put32(data,0x40000,1);put32(data,0x40004,0x40020/4);
        put32(data,0x40020,0x50000/4);put32(data,0x50000+0x2b8,0x400/4);put32(data,0x50000+0x2bc,0x8000/4);
    }
    Format format()const override{return Format::Iso;}
    uint64_t DiscSize()const override{return data.size();}
    void Read(uint64_t off,void* out,size_t size) override {
        if(off>data.size() || size>data.size()-size_t(off)) throw std::runtime_error("fixture range");
        std::memcpy(out,data.data()+off,size);
    }
    bool ReadsWiiDecrypted()const override{return decrypted;}
    void ReadWiiDecrypted(uint64_t,uint64_t,void* out,size_t size)override{std::memset(out,0,size);}
};
int main(int argc,char** argv) {
    if(argc>3 && std::string(argv[1])=="--real-plan") {
        auto bytes=[](const fs::path& p){std::ifstream f(p,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});};
        fs::path sys=fs::u8path(argv[2]);Bytes boot=bytes(sys/"boot.bin"),fst=bytes(sys/"fst.bin"),loader=bytes(sys/"apploader.img"),dol=bytes(sys/"main.dol");
        if(boot.size()<0x440||fst.size()<12||loader.size()<0x20||dol.size()<0x100)return 2;
        uint64_t fstAt=uint64_t(Be32(boot.data()+0x424))*4,dolAt=uint64_t(Be32(boot.data()+0x420))*4;
        uint64_t size=std::max(dolAt+dol.size(),fstAt+fst.size());
        unsigned count=Be32(fst.data()+8);
        for(unsigned i=1;i<count;++i)if(!fst[i*12])size=std::max(size,uint64_t(Be32(fst.data()+i*12+4))*4+Be32(fst.data()+i*12+8));
        fs::path blocked=fs::u8path(argv[3]);{std::ofstream f(blocked);f<<"scratch preflight sentinel";}
        Progress progress;
        try { ExtractTree([&](uint64_t at,void* out,size_t n){
            const Bytes* b=at==0?&boot:at==fstAt?&fst:at==dolAt?&dol:at==0x2440?&loader:nullptr;
            if(!b||n>b->size())Fail("Unexpected preflight read");std::memcpy(out,b->data(),n);
        },true,size,blocked,progress); }
        catch(const fs::filesystem_error&){std::printf("PASS real NSMB file table preflight entries=%u bytes=%llu\n",count,(unsigned long long)progress.total.load());return 0;}
        catch(const std::exception& e){std::printf("FAIL real preflight %s\n",e.what());return 2;}
        return 2;
    }
    if(argc>2 && std::string(argv[1]).find("--tree-")==0) {
        std::string mode=argv[1];
        Bytes source(0x8000);put32(source,0x420,0x2800/4);put32(source,0x424,0x3000/4);
        const unsigned count=mode=="--tree-budget"?41:mode=="--tree-deep"?66:3;Bytes fst(count*12+count*8);fst[0]=1;put32(fst,8,count);
        for(unsigned i=1;i<count;++i) {
            put32(fst,i*12,(i-1)*8);put32(fst,i*12+4,0x6000/4);put32(fst,i*12+8,0x1000);
            std::snprintf(reinterpret_cast<char*>(fst.data()+count*12+(i-1)*8),8,"f%05u",i);
        }
        if(mode=="--tree-root")fst[0]=0;
        if(mode=="--tree-name")put32(fst,12,0xffffff);
        if(mode=="--tree-unterminated")std::memset(fst.data()+count*12,'x',count*8);
        if(mode=="--tree-duplicate")put32(fst,24,0);
        if(mode=="--tree-range")put32(fst,16,0x8000/4);
        if(mode=="--tree-parent"){fst[12]=1;put32(fst,16,99);put32(fst,20,count);}
        if(mode=="--tree-dol"){put32(source,0x2800,0xffffff00);put32(source,0x2890,0x400);}
        if(mode=="--tree-deep")for(unsigned i=1;i<count;++i){fst[i*12]=1;put32(fst,i*12+4,i-1);put32(fst,i*12+8,count);}
        put32(source,0x428,uint32_t(fst.size()/4));std::memcpy(source.data()+0x3000,fst.data(),fst.size());
        Progress progress;bool rejected=false;
        try { ExtractTree([&](uint64_t at,void* out,size_t n){CheckRange(source.size(),at,n);std::memcpy(out,source.data()+at,n);},true,source.size(),fs::u8path(argv[2]),progress); }
        catch(const std::exception&) {rejected=true;}
        bool ok=mode=="--tree-valid"?!rejected:(rejected&&progress.done.load()==0);
        std::printf("%s %s written=%llu source=%zu\n",ok?"PASS":"FAIL",mode.c_str(),(unsigned long long)progress.done.load(),source.size());return ok?0:2;
    }
    if(argc>1 && std::string(argv[1])=="--decrypted-range") {
        FixtureBlob blob;blob.decrypted=true;WiiPartition part(blob);uint8_t byte;
        bool rejected=false;try { part.Read(0x7c00,&byte,1); } catch(const std::exception&) {rejected=true;}
        std::printf("%s decrypted range guard\n",rejected?"PASS":"FAIL");return rejected?0:2;
    }
    if(argc>1 && std::string(argv[1])=="--cached-range") {
        FixtureBlob blob;WiiPartition part(blob);uint8_t byte;
        part.Read(0,&byte,1);
        bool rejected=false;try { part.Read(0x7c00,&byte,1); } catch(const std::exception&) {rejected=true;}
        if(!rejected) { std::printf("FAIL cached out-of-partition read accepted\n");return 2; }
        std::printf("PASS cached range rejected\n");return 0;
    }
    throw std::runtime_error("Specify check");
}
