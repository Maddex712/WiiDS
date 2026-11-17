#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#define ZoneScopedN(x) ((void)0)
#define AURORA_ASSERT(x, ...) do { if (!(x)) throw std::runtime_error("assertion"); } while (false)
struct Driver {
  unsigned configure=0, acquire=0, presents=0, draws=0, submits=0, before=0, after=0;
  bool texture=true, presentSuccess=true;
} driver;
namespace wgpu {
enum class TextureFormat { Undefined, RGBA8Unorm, BGRA8Unorm };
enum class PresentMode { Fifo, Mailbox };
enum class CompositeAlphaMode { Auto };
enum class TextureUsage { RenderAttachment };
enum class SurfaceGetCurrentTextureStatus { SuccessOptimal, SuccessSuboptimal, Timeout, Outdated, Lost, Error };
enum class LoadOp { Clear, Load };
enum class StoreOp { Store };
enum class CallbackMode { AllowSpontaneous };
enum class QueueWorkDoneStatus { Success };
enum class Status { Success, Error };
struct ConvertibleStatus {
  bool success;
  ConvertibleStatus(Status value):success(value==Status::Success){}
  ConvertibleStatus(bool value):success(value){}
  explicit operator bool() const {return success;}
};
struct StringView {};
struct Future { uint64_t id=0; };
struct View { bool valid=false; explicit operator bool() const { return valid; } };
struct Texture { bool valid=false; explicit operator bool() const { return valid; } View CreateView() const {return {valid};} };
struct BindGroup { bool valid=false; explicit operator bool() const {return valid;} };
struct SurfaceTexture { SurfaceGetCurrentTextureStatus status; Texture texture; };
SurfaceGetCurrentTextureStatus acquireStatus=SurfaceGetCurrentTextureStatus::SuccessOptimal;
struct CommandEncoderDescriptor { const char* label; };
struct CommandBufferDescriptor { const char* label; };
struct CommandBuffer {};
struct RenderPassColorAttachment { View view; LoadOp loadOp; StoreOp storeOp; };
struct RenderPassDescriptor { const char* label; size_t colorAttachmentCount; const RenderPassColorAttachment* colorAttachments; const void* timestampWrites=nullptr; };
struct Pass {
  void SetViewport(float,float,float,float,float,float) const {}
  void SetScissorRect(uint32_t,uint32_t,uint32_t,uint32_t) const {}
  void SetPipeline(int) const {}
  void SetBindGroup(unsigned,BindGroup) const {}
  void Draw(unsigned) const {++driver.draws;}
  void End() const {}
};
struct Encoder { Pass BeginRenderPass(const RenderPassDescriptor*) const {return {};} CommandBuffer Finish(const CommandBufferDescriptor*) const {return {};} };
struct Device { Encoder CreateCommandEncoder(const CommandEncoderDescriptor*) const {return {};} };
struct SurfaceConfiguration {
  Device device; TextureFormat format; TextureUsage usage; uint32_t width=0,height=0;
  CompositeAlphaMode alphaMode; PresentMode presentMode;
};
struct Surface {
  bool valid=true; explicit operator bool() const {return valid;}
  void Configure(const SurfaceConfiguration*) const {++driver.configure;}
  void GetCurrentTexture(SurfaceTexture* value) const {++driver.acquire;*value={acquireStatus,{driver.texture}};}
  bool Present() const {++driver.presents;return driver.presentSuccess;}
};
struct Queue {
  void Submit(unsigned,const CommandBuffer*) const {++driver.submits;}
  template<class F> Future OnSubmittedWorkDone(CallbackMode,F) const {return {1};}
};
}
using WGPUSurface=void*;
struct AuroraWindowSize { uint32_t width=640,height=448,native_fb_width=640,native_fb_height=448; float scale=1; };
enum class PresentTargetStatus { Pending, Ready, TransientUnavailable, Lost, Error };
namespace webgpu {
wgpu::Device g_device;
wgpu::Queue g_queue;
int copy_pipeline(wgpu::TextureFormat,bool=false) {return 0;}
struct TextureWithSampler { wgpu::View view{true}; };
wgpu::BindGroup create_copy_bind_group(const TextureWithSampler&) {return {};}
namespace gpu_prof { const void* pass_writes(const char*) {return nullptr;} }
struct Viewport {float left=0,top=0,width=640,height=448;};
}
namespace window { bool presentable=true; struct SurfaceLock {explicit SurfaceLock(bool){}}; bool is_presentable(){return presentable;} }
namespace render_worker { bool is_worker_thread(){return true;} }
namespace imgui { using DrawData=int;void render(const wgpu::Pass&,DrawData){} }
static void before_present(){++driver.before;}
static void after_present(){++driver.after;}
static uint32_t scissor_coord(double v,uint32_t max){return std::isfinite(v)?uint32_t(std::clamp(v,0.0,double(max))):0;}
namespace present {
#include "actual_state.inc"
struct TargetSnapshot {
  std::shared_ptr<TargetState> state;uint64_t generation=1,revision=1;
  AuroraWindowSize size;
  wgpu::TextureFormat format=wgpu::TextureFormat::RGBA8Unorm;
  wgpu::PresentMode presentMode=wgpu::PresentMode::Fifo;
  wgpu::CompositeAlphaMode alphaMode=wgpu::CompositeAlphaMode::Auto;
  wgpu::TextureUsage usage=wgpu::TextureUsage::RenderAttachment;
  bool sdlManaged=false;
};
struct Request {TargetSnapshot target;webgpu::TextureWithSampler source;webgpu::Viewport viewport;wgpu::BindGroup rmlBindGroup;bool rmlOverlay=false,primary=true;imgui::DrawData imguiDrawData=0;};
#include "actual_submit.inc"
}
static unsigned checks=0,failures=0;
static void check(bool ok,const char* message){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
static present::Request fresh(){driver={};wgpu::acquireStatus=wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal;window::presentable=true;present::Request r;r.target.state=std::make_shared<present::TargetState>();return r;}
using S=wgpu::SurfaceGetCurrentTextureStatus;
int main(){
  auto r=fresh();for(int i=0;i<6;++i)present::submit(r);
  check(driver.configure==1&&driver.presents==6,"normal frames configure once and all present");
  check(driver.before==6&&driver.after==6,"primary pacing hooks preserved");
  r=fresh();wgpu::acquireStatus=S::SuccessSuboptimal;
  for(int i=0;i<600;++i)present::submit(r);
  check(driver.configure==2,"persistent suboptimal stream has one recovery configure");
  check(driver.presents==600&&driver.draws==600,"all usable suboptimal pictures presented");
  check(r.target.state->status==PresentTargetStatus::Ready,"suboptimal pictures stay ready");
  auto count=driver.configure;++r.target.revision;r.target.size.native_fb_width=800;present::submit(r);
  check(driver.configure==count+1,"real resize still reconfigures during suboptimal stream");
  count=driver.configure;
  r.target.presentMode=wgpu::PresentMode::Mailbox;present::submit(r);
  check(driver.configure==count+1,"real present-mode change still reconfigures");
  count=driver.configure;
  r.target.format=wgpu::TextureFormat::BGRA8Unorm;present::submit(r);
  check(driver.configure==count+1,"real format change still reconfigures");
  r=fresh();wgpu::acquireStatus=S::SuccessSuboptimal;present::submit(r);present::submit(r);
  wgpu::acquireStatus=S::SuccessOptimal;present::submit(r);
  wgpu::acquireStatus=S::SuccessSuboptimal;present::submit(r);present::submit(r);
  check(driver.configure==3,"optimal recovery rearms a later suboptimal episode");
  r=fresh();present::submit(r);wgpu::acquireStatus=S::Timeout;
  for(int i=0;i<10;++i)present::submit(r);
  check(driver.configure==1,"transient timeouts do not recreate a valid swapchain");
  check(driver.presents==1&&driver.before==1&&driver.after==1,"timeouts never present or run pacing hooks");
  check(r.target.state->status==PresentTargetStatus::TransientUnavailable,"timeout status preserved");
  wgpu::acquireStatus=S::SuccessOptimal;present::submit(r);
  check(driver.configure==1&&driver.presents==2,"post-timeout success reuses valid configuration");
  count=driver.configure;wgpu::acquireStatus=S::Outdated;present::submit(r);
  check(r.target.state->dirty&&r.target.state->status==PresentTargetStatus::TransientUnavailable,"outdated requires recovery");
  wgpu::acquireStatus=S::SuccessOptimal;present::submit(r);
  check(driver.configure==count+1,"outdated recovery configures");
  count=driver.configure;
  wgpu::acquireStatus=S::Lost;present::submit(r);
  check(r.target.state->dirty&&r.target.state->status==PresentTargetStatus::Lost,"surface loss still requires recovery");
  wgpu::acquireStatus=S::SuccessOptimal;present::submit(r);
  check(driver.configure==count+1,"loss recovery configures");
  r=fresh();wgpu::acquireStatus=S::Error;present::submit(r);
  check(driver.presents==0&&r.target.state->status==PresentTargetStatus::Error,"acquire errors never present");
  r=fresh();driver.texture=false;present::submit(r);
  check(driver.presents==0&&r.target.state->status==PresentTargetStatus::TransientUnavailable,"empty texture never presented");
  r=fresh();r.target.state->retired=true;present::submit(r);check(driver.acquire==0,"retired target is not acquired");
  r=fresh();++r.target.generation;present::submit(r);check(driver.acquire==0,"old generation is not acquired");
  r=fresh();r.target.sdlManaged=true;window::presentable=false;present::submit(r);check(driver.acquire==0,"nonpresentable window is not acquired");
  r=fresh();r.target.size.native_fb_height=0;present::submit(r);check(driver.acquire==0,"zero-size target is not acquired");
  r=fresh();r.primary=false;present::submit(r);check(driver.before==0&&driver.after==0&&driver.presents==1,"companion does not duplicate pacing hooks");
  r=fresh();driver.presentSuccess=false;present::submit(r);check(r.target.state->dirty&&r.target.state->status==PresentTargetStatus::Lost,"failed present still forces recovery");
  r=fresh();r.source.view={false};r.imguiDrawData=1;present::submit(r);check(driver.presents==1,"UI-only frame remains supported");
  r=fresh();wgpu::acquireStatus=S::SuccessSuboptimal;present::submit(r);
  wgpu::acquireStatus=S::Timeout;present::submit(r);
  check(driver.configure==2&&driver.presents==1,"pending suboptimal recovery runs before timeout without presenting");
  wgpu::acquireStatus=S::SuccessSuboptimal;present::submit(r);present::submit(r);
  check(driver.configure==2&&driver.presents==3,"timeout does not rearm persistent suboptimal churn");
  count=driver.configure;wgpu::acquireStatus=S::Outdated;present::submit(r);
  check(r.target.state->dirty,"outdated overrides suboptimal debounce");
  wgpu::acquireStatus=S::SuccessSuboptimal;present::submit(r);present::submit(r);
  check(driver.configure==count+1,"outdated recovers within persistent suboptimal episode");
  count=driver.configure;wgpu::acquireStatus=S::Lost;present::submit(r);
  check(r.target.state->dirty&&r.target.state->status==PresentTargetStatus::Lost,"lost overrides suboptimal debounce");
  wgpu::acquireStatus=S::SuccessSuboptimal;present::submit(r);present::submit(r);
  check(driver.configure==count+1,"lost recovers within persistent suboptimal episode");
  count=driver.configure;driver.presentSuccess=false;present::submit(r);driver.presentSuccess=true;
  present::submit(r);present::submit(r);
  check(driver.configure==count+1,"failed Present recovers within persistent suboptimal episode");
  std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
