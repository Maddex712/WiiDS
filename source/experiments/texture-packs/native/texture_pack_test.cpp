#include "texture_pack.h"
#include <cassert>

using namespace wiids::textures;
static void copy(const std::string& source,const std::string& destination) {
    File in(fopen(source.c_str(),"rb"),fclose),out(fopen(destination.c_str(),"wb"),fclose);assert(in&&out);
    char bytes[4096];while(auto n=fread(bytes,1,sizeof bytes,in.get()))assert(fwrite(bytes,1,n,out.get())==n);
}
int main(int argc,char** argv) {
    assert(argc==2);const std::string dir=argv[1];unsigned checks=0;
    assert(xxh64("abc",3,0)==0x44bc2cf5ad770999ull);++checks;
    assert(xxh64("",0,0)==0xef46db3751d8e999ull);++checks;
    Image image;
    assert(readImage(dir+"/plain.png",image)&&image.width==16&&image.height==16);++checks;
    assert(!readImage(dir+"/huge-idat.png",image));++checks;
    assert(!readImage(dir+"/inflate-bomb.png",image));++checks;
    assert(wiids_texture_decode::allocated==0);++checks;
    assert(wiids_texture_decode::allocate(wiids_texture_decode::limit+1)==nullptr);++checks;
    auto* allocation=wiids_texture_decode::allocate(64);assert(allocation);++checks;
    assert(wiids_texture_decode::resize(allocation,wiids_texture_decode::limit+1)==nullptr);++checks;
    wiids_texture_decode::release(allocation);assert(wiids_texture_decode::allocated==0);++checks;
    assert(readImage(dir+"/authored.dds",image)&&image.levels.size()==3);++checks;
    assert(image.levels[0][0]==0xff0000ff&&image.levels[1][0]==0xff00ff00&&image.levels[2][0]==0xffff0000);++checks;
    assert(!readImage(dir+"/truncated.dds",image));++checks;
    const uint8_t bytes[32]{};char hash[17];snprintf(hash,sizeof hash,"%016llx",(unsigned long long)xxh64(bytes,32,0));
    const std::string base=dir+"/tex1_8x8_m_"+hash;
    copy(dir+"/plain.png",base+"_0.png");copy(dir+"/mip1.png",base+"_0_mip1.png");copy(dir+"/mip2.png",base+"_0_mip2.png");
    copy(dir+"/plain.png",base+"_1.png");copy(dir+"/mip1.png",base+"_1_mip1.png");
    copy(dir+"/plain.png",base+"_2_arb.png");
    setenv("WIIDS_NATIVE_TEXTURE_PATHS",dir.c_str(),1);
    Pack pack;auto owner=std::make_shared<int>(1);
    assert(pack.replace(bytes,32,0,8,8,3,nullptr,0,owner,image));++checks;
    assert(image.levels.size()==3&&image.levels[1][0]==0xff00ff00&&image.levels[2][0]==0xffff0000);++checks;
    assert(!pack.replace(bytes,32,1,8,8,3,nullptr,0,owner,image));++checks;
    assert(!pack.replace(bytes,32,2,8,8,3,nullptr,0,owner,image));++checks;
    uint8_t palette[32]{};auto before=paletteSuffix(bytes,32,8,palette,32);palette[0]=1;
    assert(before!=paletteSuffix(bytes,32,8,palette,32));++checks;
    uint8_t sparse[32];memset(sparse,0x0f,sizeof sparse);uint8_t rangedPalette[64]{};
    const uint64_t stock=123,identity=paletteIdentity(stock,sparse,32,8,rangedPalette,64);
    rangedPalette[16]=1;assert(identity!=paletteIdentity(stock,sparse,32,8,rangedPalette,64));++checks;
    rangedPalette[16]=0;rangedPalette[40]=1;
    assert(identity==paletteIdentity(stock,sparse,32,8,rangedPalette,64)&&stock==123);++checks;
    printf("%u native decode, allocation, authored mip, fallback and palette checks passed\n",checks);
}
