#include "wiids_art.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
using namespace wiids::art;
void u16(Bytes& b, size_t p, unsigned v) { b[p]=v>>8; b[p+1]=v; }
void u32(Bytes& b, size_t p, unsigned v) { b[p]=v>>24; b[p+1]=v>>16; b[p+2]=v>>8; b[p+3]=v; }
Bytes font() {
    Bytes b(0xb0, 0);
    std::memcpy(b.data(),"RFNT",4); b[4]=0xfe;b[5]=0xff;u16(b,12,16);
    std::memcpy(b.data()+16,"FINF",4);u32(b,20,32);b[25]=2;b[46]=1;
    std::memcpy(b.data()+48,"TGLP",4);u32(b,52,80);
    b[56]=1;b[57]=1;u32(b,60,32);u16(b,64,1);u16(b,66,0);
    u16(b,68,1);u16(b,70,1);u16(b,72,8);u16(b,74,8);u32(b,76,80);
    for (int i=80;i<112;++i) b[i]=0xff;
    std::memcpy(b.data()+128,"CWDH",4);u32(b,132,20);b[145]=1;b[146]=1;
    std::memcpy(b.data()+148,"CMAP",4);u32(b,152,28);u16(b,156,32);u16(b,158,32);
    return b;
}
int main(int argc, char** argv) {
    int checks=0;
    auto good=font();
    auto result=BuildFontAtlas(good,16,16);
    if (result.glyphs.size()!=1 || result.glyphs.at(32).w!=1) throw std::runtime_error("valid font changed");
    ++checks;
    if (!(argc > 1 && std::string(argv[1]) == "--controls")) for (const auto& spec : {std::make_pair("FINF",23),{"TGLP",24},{"CWDH",8},{"CMAP",14}}) {
        for (int payload=0;payload<spec.second;++payload) {
            Bytes b(24+payload,0);
            std::memcpy(b.data(),"RFNT",4);b[4]=0xfe;b[5]=0xff;u16(b,12,16);
            std::memcpy(b.data()+16,spec.first,4);u32(b,20,8+payload);
            bool rejected=false;
            try { (void)BuildFontAtlas(b,16,16); } catch(const std::exception&) { rejected=true; }
            if (!rejected) throw std::runtime_error("truncated section accepted");
            ++checks;
        }
    }
    // Tables claiming glyph records beyond their own section must fail, even if later file data exists.
    if (!(argc > 1 && std::string(argv[1]) == "--controls")) for (int method : {1,2}) {
        Bytes b=good;u16(b,160,method);
        if (method==1) u16(b,158,255); else u16(b,168,1000);
        bool rejected=false;
        try { (void)BuildFontAtlas(b,16,16); } catch(const std::exception&) { rejected=true; }
        if (!rejected) throw std::runtime_error("invalid CMAP accepted");
        ++checks;
    }
    for (int i=1;i<argc;++i) {
        if (std::string(argv[i]) == "--controls") continue;
        auto real=BuildFontAtlas(ReadFile(argv[i]),2048,4096);
        if(real.glyphs.empty()) throw std::runtime_error("real font produced no glyphs");
        std::printf("FONT %s %s\n", argv[i], Sha256Hex(real.atlas.rgba).c_str());
        ++checks;
    }
    std::printf("PASS %d font cases\n",checks);
}

