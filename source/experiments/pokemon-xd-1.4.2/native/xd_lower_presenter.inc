// Included by Aurora recording.cpp; all GPU work uses its existing frame/worker.
#include "xd_presentation.h"
#include "gfx/frame_packet.hpp"
#include "gfx/present_target.hpp"
#include "webgpu/gpu.hpp"
#include <android/native_window.h>
#include <imgui.h>
#include <mutex>
#include <string>
#include <cstdio>
#include <chrono>
#include <cmath>
namespace aurora::gfx::detail { webgpu::TextureWithSampler xd_backdrop_source(); }
#include "xd_lower_draw.inc"
namespace aurora::imgui { DrawData freeze_list(const ImDrawList&,float,float) noexcept; }
extern "C" bool PortMenu_Active(void);
namespace {
std::mutex xdSurfaceMutex;
ANativeWindow* xdRequestedWindow=nullptr;
uint64_t xdSurfaceRevision=0;
int xdRequestedWidth=0,xdRequestedHeight=0;
struct XdLowerSurface {
    ANativeWindow* window=nullptr;
    aurora::gfx::PresentTargetId target=0;
    uint64_t revision=UINT64_MAX;
    int width=0,height=0;
} xdLower;
void xdSurfaceReconcile(){
    using namespace aurora;
    std::lock_guard hold(xdSurfaceMutex);
    if(xdLower.revision==xdSurfaceRevision)return;
    WiidsXD_LowerReadySet(0);
    if(xdLower.target)gfx::destroy_present_target(xdLower.target);
    if(xdLower.window)ANativeWindow_release(xdLower.window);
    xdLower={};xdLower.revision=xdSurfaceRevision;
    if(!xdRequestedWindow||xdRequestedWidth<=0||xdRequestedHeight<=0)return;
    xdLower.window=xdRequestedWindow;ANativeWindow_acquire(xdLower.window);
    xdLower.width=xdRequestedWidth;xdLower.height=xdRequestedHeight;
    wgpu::SurfaceSourceAndroidNativeWindow native;native.window=xdLower.window;
    wgpu::SurfaceDescriptor desc;desc.nextInChain=&native;desc.label="XD passive lower surface";
    auto surface=webgpu::g_instance.CreateSurface(&desc);
    wgpu::SurfaceCapabilities caps;
    if(!surface||surface.GetCapabilities(webgpu::g_adapter,&caps)!=wgpu::Status::Success)return;
    bool format=false,alpha=false;
    auto alphaMode=wgpu::CompositeAlphaMode::Premultiplied;
    for(size_t i=0;i<caps.formatCount;i++)format|=caps.formats[i]==gfx::color_format();
    for(size_t i=0;i<caps.alphaModeCount;i++)alpha|=caps.alphaModes[i]==wgpu::CompositeAlphaMode::Premultiplied;
    if(!alpha)for(size_t i=0;i<caps.alphaModeCount;i++)if(caps.alphaModes[i]==wgpu::CompositeAlphaMode::Inherit){
        alpha=true;alphaMode=wgpu::CompositeAlphaMode::Inherit;break;
    }
    // The existing ImGui pipeline uses the EFB format. Transparent field overlays
    // need premultiplied alpha; fail closed to ordinary upper UI otherwise.
    if(!format||!alpha)return;
    xdLower.target=gfx::create_present_target({.label="XD lower",.surface=std::move(surface),
        .width=(uint32_t)xdLower.width,.height=(uint32_t)xdLower.height,
        .preferredFormat=gfx::color_format(),.preferredAlphaMode=alphaMode,.vsync=false});
    WiidsXD_LowerReadySet(xdLower.target!=0);
}
uint64_t xdMeshRevision=0;
aurora::imgui::DrawData xdLowerDraw(const XdPresentationState& s,int width,int height,ImTextureID top=0,float topAspect=16.f/9.f,float light=1,float skyW=0,float skyH=0){
    static std::string previousKey;
    static aurora::imgui::DrawData cached;
    std::string key;
    auto add=[&](const void* p,size_t n){key.append((const char*)p,n);};
    for(int v:{width,height,s.kind,s.row,s.item,s.party_slot,s.lower_owner})add(&v,sizeof(v));
    if(s.kind==XD_SCREEN_ROOT||s.kind==XD_SCREEN_SUMMARY||s.kind==XD_SCREEN_BAG||s.kind==XD_SCREEN_OPTIONS||s.kind==XD_SCREEN_DETAIL||s.kind==XD_SCREEN_COMMAND||s.kind==XD_SCREEN_MOVES||s.kind==XD_SCREEN_TARGET){add(&s.values.choice_count,sizeof(s.values.choice_count));add(s.values.choices,s.values.choice_count*sizeof(XdChoice));}
    if(s.kind==XD_SCREEN_PARTY){add(s.values.party,sizeof(s.values.party));add(s.levels,sizeof(s.levels));}
    // Design pass 8: every lower header shows the party's ball pips and, in battle, the actor ring.
    if(s.kind!=XD_SCREEN_FIELD&&s.kind!=XD_SCREEN_PARTY){add(s.values.party,sizeof(s.values.party));add(&s.values.actor_party_slot,sizeof(s.values.actor_party_slot));}
    if(s.kind==XD_SCREEN_COMMAND||s.kind==XD_SCREEN_MOVES||s.kind==XD_SCREEN_TARGET){
        add(&s.values.actor,sizeof(s.values.actor));add(s.values.acting.name,sizeof(s.values.acting.name));
        add(s.values.moves,sizeof(s.values.moves));add(s.values.move_type_names,sizeof(s.values.move_type_names));
    }
    if(s.kind==XD_SCREEN_BAG||s.kind==XD_SCREEN_OPTIONS||s.kind==XD_SCREEN_DETAIL||s.kind==XD_SCREEN_PARTY){
        add(&s.tab,sizeof(s.tab));add(&s.page,sizeof(s.page));add(s.counts,sizeof(s.counts));add(s.tabs,sizeof(s.tabs));add(s.detail,sizeof(s.detail));add(&s.legal_mask,sizeof(s.legal_mask));add(&s.active_mask,sizeof(s.active_mask));
    }
    if(s.kind==XD_SCREEN_SUMMARY){add(s.values.moves,sizeof(s.values.moves));add(s.values.move_type_names,sizeof(s.values.move_type_names));}
    if(s.kind==XD_SCREEN_SUMMARY)add(&xdSummaryImage,sizeof(xdSummaryImage));
    add(&s.values.menu_id,sizeof(s.values.menu_id));add(&s.values.battle,sizeof(s.values.battle));add(&s.title_sky,sizeof(s.title_sky));int l=(int)std::lround(light*255);add(&l,sizeof(l));if(s.title_sky){add(&top,sizeof(top));add(&skyW,sizeof(skyW));add(&skyH,sizeof(skyH));}
    if(cached&&key==previousKey)return cached;
    ++xdMeshRevision;xdHitCount=0;
    // No ImGui windows/widgets, input, focus, or navigation. Only immutable mesh.
    ImDrawList list(ImGui::GetDrawListSharedData());list._ResetForNewFrame();
    list.PushTextureID(ImGui::GetIO().Fonts->TexID);list.PushClipRect({0,0},{(float)width,(float)height});
    xdLowerPaint(list,s,width,height,top,topAspect,skyW,skyH);
    xdLowerFade(list,width,height,light);
    list.PopClipRect();list.PopTextureID();
    WiidsXD_TouchRegions(xdHits,xdHitCount);
    previousKey=key;cached=aurora::imgui::freeze_list(list,(float)width,(float)height);return cached;
}
aurora::imgui::DrawData xdTopDraw(int width,int height){
    XdPresentationState s{};WiidsXD_PresentReadV38(&s);
    if(!s.lower_owner||(s.kind!=XD_SCREEN_BAG&&s.kind!=XD_SCREEN_OPTIONS&&s.kind!=XD_SCREEN_SUMMARY))return {};
    s.page=s.kind==XD_SCREEN_SUMMARY?10:s.page;s.kind=XD_SCREEN_DETAIL;
    ImDrawList list(ImGui::GetDrawListSharedData());list._ResetForNewFrame();
    list.PushTextureID(ImGui::GetIO().Fonts->TexID);list.PushClipRect({0,0},{(float)width,(float)height});
    xdLowerPaint(list,s,width,height);list.PopClipRect();list.PopTextureID();
    return aurora::imgui::freeze_list(list,(float)width,(float)height);
}
void xdQueueLower(aurora::gfx::detail::FramePacket& frame,const aurora::webgpu::TextureWithSampler& modelSource){
    using namespace aurora;
    if(!imgui::is_initialized())return;
    xdSurfaceReconcile();if(!xdLower.target)return;
    auto status=gfx::present_target_status(xdLower.target);
    if(status==gfx::PresentTargetStatus::Lost||status==gfx::PresentTargetStatus::Error){
        WiidsXD_LowerReadySet(0);return;
    }
    XdPresentationState s{};WiidsXD_PresentReadV38(&s);
    // Opaque services remain a compatibility path above until their own split.
    if(PortMenu_Active()&&s.kind!=XD_SCREEN_OPTIONS){s.kind=XD_SCREEN_COMPAT;s.lower_owner=0;}
    auto target=gfx::present::snapshot(xdLower.target);
    gfx::present::Request request{.target=std::move(target)};
    request.viewport={0,0,(float)xdLower.width,(float)xdLower.height};
    xdSummaryImage=s.kind==XD_SCREEN_SUMMARY?reinterpret_cast<ImTextureID>(modelSource.view.Get()):0;
    // Design pass 10: the top frame (this frame's display copy, written before the presents run) is
    // sampled directly on the GPU for the title sky; no readback. The lower light follows the top's
    // fades: WiidsXD_TopLight comes from EFB peeks before GXCopyDisp. Gradual fades are followed
    // closely (50 ms smoothing); a cut to or from black becomes an eased 300 ms fade.
    static float light=0,rampFrom=0,rampTo=0,rampT=1;static auto last=std::chrono::steady_clock::now();
    const auto now=std::chrono::steady_clock::now();
    const float dt=std::min(0.1f,std::chrono::duration<float>(now-last).count());last=now;
    // Keep the exchange black across the native window gap when speakers change.
    static auto dialogBlackUntil=now;
    if(!s.values.battle&&!s.title_sky&&!s.lower_owner&&(s.kind==XD_SCREEN_COMPAT||(now<dialogBlackUntil&&s.kind==XD_SCREEN_FIELD&&s.transition)))
        dialogBlackUntil=now+std::chrono::milliseconds(750);
    const bool dialogHold=!s.values.battle&&!s.title_sky&&now<dialogBlackUntil;
    const float aim=dialogHold?0.f:(s.values.battle||s.kind==XD_SCREEN_FIELD||s.lower_owner&&!s.title_sky?1.f:(!s.title_sky&&!s.lower_owner?0.f:WiidsXD_TopLight()/1000.f));
    if(std::fabs(aim-rampTo)>0.35f){rampFrom=light;rampTo=aim;rampT=0;}
    if(rampT<1){rampT=std::min(1.f,rampT+dt/0.3f);light=rampFrom+(rampTo-rampFrom)*rampT*rampT*(3-2*rampT);}
    else{rampTo=aim;light+=(aim-light)*(1-std::exp(-dt/0.05f));if(std::fabs(aim-light)<0.004f)light=aim;}
    // Design pass 10c: the title sky is the top's backdrop (sky and cloud models, copied before the logo,
    // messages and menus are drawn; recording.cpp copy_xd_backdrop), using the persistent GPU copy. sky* is
    // its size as the top display shows it, so the lower screen uses the top's own scale.
    ImTextureID top=0;float topAspect=16.f/9.f,skyW=0,skyH=0;
    if(s.title_sky){
        const auto src=gfx::detail::xd_backdrop_source();
        if(src.view&&src.size.width&&src.size.height){
            const auto& fb=frame.efb.xfb.size;
            const float scale=fb.native_fb_width&&fb.native_fb_height?
                webgpu::calculate_present_viewport(fb.native_fb_width,fb.native_fb_height,src.size.width,src.size.height).height/src.size.height:1.f;
            skyW=src.size.width*scale;skyH=src.size.height*scale;
            // XD's EFB display copy carries zero alpha; the top present ignores it.
            // Give only the lower sky's sampling view opaque alpha as well.
            static wgpu::Texture skyTexture;static wgpu::TextureView skyView;
            if(skyTexture.Get()!=src.texture.Get()){
                skyTexture=src.texture;
                wgpu::TextureComponentSwizzleDescriptor swizzle;
                swizzle.swizzle.r=wgpu::ComponentSwizzle::R;swizzle.swizzle.g=wgpu::ComponentSwizzle::G;
                swizzle.swizzle.b=wgpu::ComponentSwizzle::B;swizzle.swizzle.a=wgpu::ComponentSwizzle::One;
                wgpu::TextureViewDescriptor desc;desc.nextInChain=&swizzle;
                skyView=skyTexture.CreateView(&desc);
            }
            top=reinterpret_cast<ImTextureID>(skyView.Get());
            topAspect=(float)src.size.width/(float)src.size.height;
        }
    }
    request.imguiDrawData=xdLowerDraw(s,xdLower.width,xdLower.height,top,topAspect,light,skyW,skyH);

    static uint64_t submittedMesh=UINT64_MAX,submittedSurface=UINT64_MAX;
    if(!s.title_sky&&s.kind!=XD_SCREEN_SUMMARY&&submittedMesh==xdMeshRevision&&submittedSurface==xdLower.revision)return;
    submittedMesh=xdMeshRevision;submittedSurface=xdLower.revision;
    // Copied mesh and target generation travel in the same frame packet as top.
    // No final-world clone, CPU readback, second scene render or unused-surface wait.
    frame.presents.push_back(std::move(request));
}
}
extern "C" void WiidsXD_LowerWindow(void* window,int width,int height){
    std::lock_guard hold(xdSurfaceMutex);
    WiidsXD_LowerReadySet(0);
    if(window)ANativeWindow_acquire((ANativeWindow*)window);
    if(xdRequestedWindow)ANativeWindow_release(xdRequestedWindow);
    xdRequestedWindow=(ANativeWindow*)window;xdRequestedWidth=width;xdRequestedHeight=height;++xdSurfaceRevision;
}
