#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <utility>
struct ANativeWindow{int refs=1,format=1;};
int ANativeWindow_getFormat(ANativeWindow* w){return w->format;}
int acquires,releases;void ANativeWindow_acquire(ANativeWindow* w){++acquires;++w->refs;}
void ANativeWindow_release(ANativeWindow* w){++releases;if(--w->refs<0)std::abort();}
int ready;void WiidsXD_LowerReadySet(int value){ready=value;}
bool allowSurface=true,allowTarget=true;
namespace wgpu {
enum class Status{Success,Error};enum class CompositeAlphaMode{Premultiplied,Inherit};
struct SurfaceSourceAndroidNativeWindow{ANativeWindow* window;};
struct SurfaceDescriptor{SurfaceSourceAndroidNativeWindow* nextInChain;const char* label;};
struct SurfaceCapabilities{size_t formatCount=1;int formats[1]={1};size_t alphaModeCount=1;CompositeAlphaMode alphaModes[1]={CompositeAlphaMode::Premultiplied};};
struct Surface{
 bool exists=true;explicit operator bool()const{return exists;}
 Status GetCapabilities(int,SurfaceCapabilities*)const{return exists?Status::Success:Status::Error;}
};
struct Instance{Surface CreateSurface(SurfaceDescriptor*){return {allowSurface};}};
}
namespace aurora {
namespace webgpu{wgpu::Instance g_instance;int g_adapter;}
namespace gfx{
using PresentTargetId=uint64_t;enum class PresentTargetStatus{Pending,Ready,TransientUnavailable,Lost,Error};
int creates,destroys,retirementWaits;PresentTargetId next=1;std::map<PresentTargetId,PresentTargetStatus> targets;
int color_format(){return 1;}
struct Desc{const char* label;wgpu::Surface surface;uint32_t width,height;int preferredFormat;wgpu::CompositeAlphaMode preferredAlphaMode;bool vsync;};
PresentTargetId create_present_target(Desc){++creates;if(!allowTarget)return 0;auto id=next++;targets[id]=PresentTargetStatus::Pending;return id;}
void destroy_present_target(PresentTargetId id){if(targets.erase(id)){++destroys;++retirementWaits;}}
PresentTargetStatus present_target_status(PresentTargetId id){auto i=targets.find(id);return i==targets.end()?PresentTargetStatus::Error:i->second;}
}}
namespace {
#include "surface_state.inc"
}
#include "surface_setter.inc"
int checks;
void check(bool b,const char* message){++checks;if(!b){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
int main(int argc,char** argv){
 using namespace aurora::gfx;ANativeWindow a,b;
 check(argc==2,"scenario required");bool duplicateOnly=argv[1][0]=='d';
 WiidsXD_LowerWindow(&a,640,480);xdSurfaceReconcile();
 check(ready==1&&creates==1&&destroys==0&&a.refs==3,"initial target and borrowed-window ownership");
 auto first=xdLower.target;auto revision=xdSurfaceRevision;auto acq=acquires,rel=releases;
 if(duplicateOnly){
  for(int i=0;i<1000;i++){WiidsXD_LowerWindow(&a,640,480);check(ready==1,"duplicate must not clear readiness");xdSurfaceReconcile();}
  check(xdSurfaceRevision==revision,"duplicate must not change revision");
  check(creates==1&&destroys==0&&retirementWaits==0,"1000 duplicates must not recreate or retire/wait for GPU");
  check(acquires==acq&&releases==rel&&a.refs==3,"healthy duplicates do no native ownership work");
  targets[first]=PresentTargetStatus::Ready;WiidsXD_LowerWindow(&a,640,480);xdSurfaceReconcile();check(xdLower.target==first&&ready==1,"Ready duplicates stable");
 }else{
  for(auto status:{PresentTargetStatus::Lost,PresentTargetStatus::Error,PresentTargetStatus::TransientUnavailable}){
   auto old=xdLower.target;targets[old]=status;WiidsXD_LowerWindow(&a,640,480);check(ready==0,"unhealthy duplicate schedules recovery");xdSurfaceReconcile();check(xdLower.target&&xdLower.target!=old&&ready==1&&a.refs==3,"unhealthy target recreates with balanced refs");
  }
  auto old=xdLower.target;WiidsXD_LowerWindow(&a,800,600);xdSurfaceReconcile();check(xdLower.target!=old&&xdLower.width==800,"resize still recreates");
  old=xdLower.target;a.format=2;WiidsXD_LowerWindow(&a,800,600);xdSurfaceReconcile();check(xdLower.target!=old,"format change with same window and size still recreates");
  WiidsXD_LowerWindow(&b,800,600);xdSurfaceReconcile();check(a.refs==1&&b.refs==3,"replacement balances old/new refs");
  WiidsXD_LowerWindow(nullptr,0,0);xdSurfaceReconcile();check(b.refs==1&&!xdLower.target&&ready==0,"teardown releases all owned refs");
  revision=xdSurfaceRevision;WiidsXD_LowerWindow(nullptr,0,0);xdSurfaceReconcile();check(xdSurfaceRevision==revision,"duplicate null is a no-op");
  allowSurface=false;WiidsXD_LowerWindow(&a,640,480);xdSurfaceReconcile();check(!ready&&!xdLower.target&&a.refs==3,"failed surface creation retains retryable request");
  allowSurface=true;WiidsXD_LowerWindow(&a,640,480);xdSurfaceReconcile();check(ready&&xdLower.target&&a.refs==3,"identical callback retries failed surface creation");
  allowTarget=false;WiidsXD_LowerWindow(&a,801,600);xdSurfaceReconcile();check(!ready&&!xdLower.target,"failed target creation");
  allowTarget=true;WiidsXD_LowerWindow(&a,801,600);xdSurfaceReconcile();check(ready&&xdLower.target&&a.refs==3,"identical callback retries failed target creation");
 }
 WiidsXD_LowerWindow(nullptr,0,0);xdSurfaceReconcile();
 check(a.refs==1&&b.refs==1,"final borrowed refs retained; no leaks or double releases");
 std::printf("PASS %s %d checks; created=%d destroyed=%d retirement-paths=%d\n",argv[1],checks,creates,destroys,retirementWaits);
}
