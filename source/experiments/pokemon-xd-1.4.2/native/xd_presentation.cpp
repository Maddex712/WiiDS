#include "xd_presentation.h"
#include <mutex>
#include <atomic>
#include <aurora/gfx.hpp>
namespace {
std::mutex presentationMutex;
XdPresentationState presentation{};
std::atomic_int lowerReady{0};
std::atomic_bool titleProbeEnabled{false},fieldProbeEnabled{false},battleNavEnabled{false};
int drawOwner=0,topPolicy=0; // game-thread only; ownership is embedded per native primitive
uint64_t cleanFrame=0;
}
extern "C" void WiidsXD_PresentPublish(const XdPresentationState* s) {
    if(!s||s->kind<0||s->kind>XD_SCREEN_DETAIL||s->values.choice_count>XD_MAX_CHOICES)return;
    std::lock_guard guard(presentationMutex);presentation=*s;
    fieldProbeEnabled.store(s->kind==XD_SCREEN_FIELD&&!s->values.battle,std::memory_order_relaxed);
    titleProbeEnabled.store(s->title_sky!=0,std::memory_order_relaxed);
    battleNavEnabled.store(s->values.battle!=0,std::memory_order_relaxed);
}
extern "C" int WiidsXD_PresentReadV38(XdPresentationState* out) {
    if(!out)return 0;std::lock_guard guard(presentationMutex);*out=presentation;
    return out->visible!=0;
}
extern "C" int WiidsXD_LowerReady(){return lowerReady.load(std::memory_order_acquire);}
extern "C" void WiidsXD_LowerReadySet(int ready){lowerReady.store(ready!=0,std::memory_order_release);}
extern "C" int WiidsXD_DrawOwner(int owner){int old=drawOwner;drawOwner=owner;return old;}
extern "C" int WiidsXD_DrawOwnerGet(){return drawOwner;}
extern "C" int WiidsXD_TopPolicy(){return topPolicy;}
extern "C" void WiidsXD_TopPolicySet(int policy){topPolicy=policy;}
extern "C" void WiidsXD_CleanCaptured(uint32_t){cleanFrame=uint64_t(aurora::gfx::current_frame())+1;}
extern "C" uint64_t WiidsXD_CleanFrame(){return cleanFrame;}

static void xdAscii(uint16_t* out,const char* in,int cap){int i=0;if(in)for(;i<cap-1&&in[i];i++)out[i]=(unsigned char)in[i];out[i]=0;}
extern "C" void WiidsXD_OptionsPublish(int n,const char* const* names,const char* const* values,int row,const char* help){
 XdPresentationState s{};WiidsXD_PresentReadV38(&s);s.kind=XD_SCREEN_OPTIONS;s.visible=1;s.lower_owner=WiidsXD_LowerReady();s.row=row;s.item=-1;s.values.choice_count=0;
 int first=row>4?row-4:0;if(first>n-5)first=n>5?n-5:0;
 for(int i=first;i<n&&i<first+5;i++){auto& c=s.values.choices[s.values.choice_count++];c={};c.action=XD_SELECT;c.argument=i;c.cursor=i;c.item_id=-1;c.enabled=1;xdAscii(c.label,names[i],XD_TEXT);int k=0;while(c.label[k]&&k<XD_TEXT-1)k++;if(values[i]&&values[i][0]&&k<XD_TEXT-3){c.label[k++]=' ';c.label[k++]=':';c.label[k++]=' ';xdAscii(c.label+k,values[i],XD_TEXT-k);}}
 xdAscii(s.detail,help,2048);s.detail_title[0]=0;WiidsXD_PresentPublish(&s);
}
extern "C" void WiidsXD_OptionsClose(){XdPresentationState s{};s.visible=1;WiidsXD_PresentPublish(&s);}

#include <cstring>
#include <cstddef>
extern "C" int WiidsXD_PresentRead(void* out){if(!out)return 0;std::lock_guard guard(presentationMutex);std::memcpy(out,&presentation,offsetof(XdPresentationState,tab));return presentation.visible!=0;}

// Called on the game thread just before GXCopyDisp, when the EFB holds the
// finished top picture including any fade layer. 24 ARGB peeks (async, read back a few frames later)
// give the brightest point m, taken relative to the scene's settled brightness ref. A fade is a
// gradual fall, followed proportionally below 90%; a one-frame drop to a darker picture that is not black is a
// scene cut and simply becomes the new ref; ref also re-settles when m holds still above near-black.
// So only real fades and cuts to black dim the lower screen; pause, menus and dark scenes do not.
#include <dolphin/gx/GXAurora.h>
#include "gx/readback.hpp"
#include "gfx/readback/readback.hpp"
extern "C" void WiidsXD_PeekDisplay(AuroraReadbackStream);
namespace { std::atomic_int fieldColor{0xA0A0A0}; std::atomic_int topLight{1000}; std::atomic_int titleFloor{-1}; std::atomic_int skyClean{1}; }
extern "C" int WiidsXD_FieldColor(){return fieldColor.load(std::memory_order_relaxed);}
extern "C" int WiidsXD_TopLight(){return topLight.load(std::memory_order_relaxed);}
extern "C" void WiidsXD_TitleFloor(int32_t group){titleFloor.store(group,std::memory_order_relaxed);}
extern "C" int32_t WiidsXD_TitleFloorGet(){return titleFloor.load(std::memory_order_relaxed);}
// The title sky samples the top's left edge (u 0..0.11). Three peeks on it tell whether a UI panel covers
// it (neutral grey, as the continue summary's Hero's name/Play time panel does); then the lower screen
// keeps pass 9's background rather than smearing that panel. Six equal readings flip the state.
extern "C" int WiidsXD_TopSkyClean(){return skyClean.load(std::memory_order_relaxed);}
extern "C" void WiidsXD_TopProbe(){
    const bool title=titleProbeEnabled.load(std::memory_order_relaxed),field=fieldProbeEnabled.load(std::memory_order_relaxed);
    if(!title&&!field)return;
    static uint64_t sampleFrame=0;const auto f=uint64_t(aurora::gfx::current_frame());
    if(f<sampleFrame)return;sampleFrame=f+(title?2:60);
    enum{NX=6,NY=4,G=NX*NY,N=G+3};
    static AuroraReadbackStream stream=0;
    static float ref=24;static int prev=-1,still=0;
    if(!stream)stream=AuroraReadbackCreateStream();
    if(!stream)return;
    u32 px[N];AuroraReadbackDest dest[N];
    for(int i=0;i<N;i++)dest[i]={(u64)i,&px[i],sizeof(u32),GX_FALSE};
    if(AuroraReadbackGet(stream,dest,N)){
        int m=0,rs=0,gs=0,bs=0,found=0;
        static int run=0;int panel=0;
        for(int i=G;i<N;i++)if(dest[i].found){
            const u32 c=px[i];const int r=(c>>16)&255,g=(c>>8)&255,b=c&255;
            panel+=std::max({r,g,b})-std::min({r,g,b})<10&&((r*77+g*150+b*29)>>8)>12;}
        const int clean=panel<2;
        run=clean==skyClean.load(std::memory_order_relaxed)?0:run+1;
        if(run>=6){skyClean.store(clean,std::memory_order_relaxed);run=0;}
        for(int i=0;i<G;i++)if(dest[i].found){
            const u32 c=px[i];rs+=(c>>16)&255;gs+=(c>>8)&255;bs+=c&255;++found;m=std::max<int>(m,(int)((((c>>16)&255)*77+((c>>8)&255)*150+(c&255)*29)>>8));}
        if(field&&found)fieldColor.store(((rs/found)<<16)|((gs/found)<<8)|(bs/found),std::memory_order_relaxed);
        const bool cut=prev>=0&&m>3&&m*2<prev&&prev-m>24;
        still=std::abs(m-prev)<=2?still+1:0;prev=m;
        if(m>ref||cut||(still>=20&&m>=24))ref=(float)std::max(m,24);
        const float r=m<=3?0.f:std::min(1.f,m/ref/0.9f);   // proportional: the top's own fade curve
        topLight.store((int)std::lround(r*1000),std::memory_order_relaxed);
    }
    // XD's EFB is 640x448; the grid stays inside it.
    AuroraReadbackPeekRequest req[N];
    for(int k=0;k<3;k++)req[G+k]={(u64)(G+k),AURORA_READBACK_PEEK_ARGB,19,(u16)(76+k*56)};
    for(int j=0;j<NY;j++)for(int i=0;i<NX;i++){
        const int k=j*NX+i;req[k]={(u64)k,AURORA_READBACK_PEEK_ARGB,(u16)(48+i*544/(NX-1)),(u16)(40+j*368/(NY-1))};}
    // The Android port stages game GX in display lists and presents without GXCopyDisp.
    // At its drained frame boundary, use the existing peek handler directly.
    std::vector<uint8_t> bytes;bytes.reserve(21+N*13);
    auto be=[&](uint64_t v,int n){for(int i=n-1;i>=0;i--)bytes.push_back((uint8_t)(v>>(i*8)));};
    be(stream,8);be(aurora::gfx::readback::next_sequence(stream),8);be(N,4);be(GX_READ_FF,1);
    for(const auto& q:req){be(q.key,8);be(q.kind,1);be(q.x,2);be(q.y,2);}
    aurora::ByteReader reader(bytes.data(),bytes.size());aurora::gx::readback::handle_capture_peeks(reader);
    WiidsXD_PeekDisplay(stream);
}

extern "C" int WiidsXD_BattleNavigationActive(){return battleNavEnabled.load(std::memory_order_relaxed);}

namespace {
XdTouchRegion touchRegions[160];int regionCount=0;
struct Touch {bool pending=false;int kind=0,menu=0,action=0,arg=0,cursor=-1;uint64_t until=0;} touch;
}
extern "C" void WiidsXD_TouchRegions(const XdTouchRegion* r,int n){
    std::lock_guard guard(presentationMutex);regionCount=std::clamp(n,0,160);std::copy(r,r+regionCount,touchRegions);
}
extern "C" int WiidsXD_LowerTouch(float x,float y){
    std::lock_guard guard(presentationMutex);const auto& s=presentation;
    if(!s.lower_owner||!s.visible||s.transition||s.kind==XD_SCREEN_FIELD||s.kind==XD_SCREEN_COMPAT)return 0;
    for(int i=regionCount-1;i>=0;--i){const auto& r=touchRegions[i];
        if(x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h){
            touch={true,s.kind,(int)s.values.menu_id,r.action,r.argument,r.cursor,WiidsXD_Now()+350};return 1;}}
    return 0;
}
extern "C" int WiidsXD_TouchTake(int kind,int* action,int* arg,int* cursor){
    std::lock_guard guard(presentationMutex);if(!touch.pending)return 0;
    if(WiidsXD_Now()>touch.until||presentation.kind!=touch.kind||(int)presentation.values.menu_id!=touch.menu||!presentation.lower_owner||presentation.transition){touch.pending=false;return 0;}
    if(kind>=0&&kind!=touch.kind)return 0;
    touch.pending=false;*action=touch.action;*arg=touch.arg;*cursor=touch.cursor;return 1;
}
