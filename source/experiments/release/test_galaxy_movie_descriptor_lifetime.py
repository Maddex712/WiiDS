"""Run production Vulkan descriptor/deferred-delete methods against handle reuse.

The fake Vulkan driver tracks descriptor resource lifetimes, permits image-view
handle recycling, and rejects sampling a descriptor after its view is destroyed.
No game, Vulkan device, or owner files are opened.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

R = Path(__file__).resolve().parents[2]
O = R / 'build/github-bugfix-2026-10-26/movie/descriptor-lifetime'
O.mkdir(parents=True, exist_ok=True)
ap = argparse.ArgumentParser()
ap.add_argument('--before', action='store_true')
ap.add_argument('--arm64', action='store_true', help='Compile only; never connect to a device.')
a = ap.parse_args()
S = R / 'reference/galaxy-android/platform/src/gx/vk'
common_path = O / 'before/vk_common.cpp' if a.before else S / 'vk_common.cpp'
sources = [common_path, S/'vk_descriptor_manager.cpp', S/'vk_target_manager.cpp', S/'vk_types.h']
common, descriptors, targets, types = [p.read_text(encoding='utf-8') for p in sources]


def method(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    for end in range(brace + 1, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if not depth:
            return text[start:end+1]
    raise AssertionError(signature)


vk_include = R / '.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include/vulkan/vulkan.h'
fixture = r'''
#define VK_USE_64_BIT_PTR_DEFINES 0
''' + '#include "' + vk_include.as_posix() + '"\n' + r'''
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <vector>
#define LOGE(...) ((void)0)
static int checks=0, failures=0;
void check(bool value,const char* message){++checks;if(!value){++failures;printf("FAIL: %s\n",message);}}
struct FakeView {uint64_t generation=0;int value=0;bool alive=false;};
struct FakeSet {std::array<VkImageView,8> views{};std::array<uint64_t,8> generations{};bool valid=true;};
static std::unordered_map<VkImageView,FakeView> driverViews;
static std::unordered_map<VkDescriptorSet,FakeSet> driverSets;
static VkDescriptorSet nextSet=1000;
static int allocationCount=0, imageDeletes=0, memoryDeletes=0;
static std::vector<char> events;
void makeView(VkImageView handle,int value){auto& v=driverViews[handle];++v.generation;v.value=value;v.alive=true;}
int sample(VkDescriptorSet set,int unit){
 auto it=driverSets.find(set);if(it==driverSets.end()||!it->second.valid)return -999;
 auto& d=it->second;auto& v=driverViews[d.views[unit]];
 return v.alive&&v.generation==d.generations[unit]?v.value:-999;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice,const VkDescriptorSetAllocateInfo*,VkDescriptorSet* set){
 *set=nextSet++;driverSets[*set]={};++allocationCount;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice,VkDescriptorPool,VkDescriptorPoolResetFlags){driverSets.clear();return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice,uint32_t count,const VkWriteDescriptorSet* writes,uint32_t,const VkCopyDescriptorSet*){
 for(uint32_t i=0;i<count;++i){auto& d=driverSets[writes[i].dstSet];uint32_t u=writes[i].dstBinding;
 d.views[u]=writes[i].pImageInfo->imageView;d.generations[u]=driverViews[d.views[u]].generation;}
}
VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice,VkDescriptorPool,uint32_t count,const VkDescriptorSet* sets){
 for(uint32_t i=0;i<count;++i)driverSets.erase(sets[i]);events.push_back('F');return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice,VkImageView view,const VkAllocationCallbacks*){
 driverViews[view].alive=false;
 for(auto& entry:driverSets)for(auto used:entry.second.views)if(used==view)entry.second.valid=false;
 events.push_back('V');
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice,VkImage,const VkAllocationCallbacks*){++imageDeletes;events.push_back('I');}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice,VkDeviceMemory,const VkAllocationCallbacks*){++memoryDeletes;events.push_back('M');}
namespace gpu {
''' + types[types.index('struct SamplerDescKey {'):types.index('struct FrameResource {')] + r'''
struct TexImage {uint64_t vkGeneration=0,vkImage=0,vkMemory=0,vkView=0;~TexImage();};
class VkRenderer {public:
 VkDevice mDevice=reinterpret_cast<VkDevice>(1);
 VkDescriptorPool mDescriptorPool=1;
 VkDescriptorSetLayout mTextureDescLayout=1;
 uint64_t mFrameNumber=1,mSamplerDescEpoch=0;
 std::unordered_map<SamplerDescKey,SamplerSetEntry,SamplerDescKeyHash> mSamplerDescSets;
 bool waitInFlightSubmissions(const char*){return true;}
 void flushDeferredDeletes();
 void invalidateSamplerDescSetsForView(VkImageView);
 VkDescriptorSet getSamplerDescriptorSet(const std::array<VkImageView,8>&,const std::array<VkSampler,8>&);
};
''' + common[common.index('struct VkPendingDelete {'):common.index('void VkRenderer::trimFrameCaches(')] + '\n' + method(targets,'void VkRenderer::invalidateSamplerDescSetsForView(') + '\n' + method(descriptors,'VkDescriptorSet VkRenderer::getSamplerDescriptorSet(') + r'''
}
int main(){
 using namespace gpu;
 sVkDeviceGeneration=7;VkRenderer renderer;
 std::array<VkImageView,8> views;views.fill(99);views[0]=10;views[1]=11;views[2]=12;
 std::array<VkSampler,8> samplers;samplers.fill(1);
 makeView(99,255);makeView(10,42);makeView(11,128);makeView(12,160);makeView(20,77);
 auto first=renderer.getSamplerDescriptorSet(views,samplers);
 check(sample(first,0)==42&&sample(first,1)==128&&sample(first,2)==160,"initial three movie planes sample correctly");
 check(renderer.getSamplerDescriptorSet(views,samplers)==first&&allocationCount==1,"unchanged live texture set uses cache");
 auto otherViews=views;otherViews[0]=20;otherViews[1]=otherViews[2]=99;
 auto unrelated=renderer.getSamplerDescriptorSet(otherViews,samplers);
 auto sharingViews=views;sharingViews[0]=20;
 auto sharing=renderer.getSamplerDescriptorSet(sharingViews,samplers);
 {
  TexImage retired;retired.vkGeneration=7;retired.vkImage=101;retired.vkMemory=201;retired.vkView=11;
 }
 check(driverViews[11].alive,"TexImage destruction only queues retirement");
 events.clear();renderer.flushDeferredDeletes();
 check(!driverViews[11].alive&&imageDeletes==1&&memoryDeletes==1,"flush destroys the queued same-device resource exactly once");
 check(renderer.mSamplerDescSets.size()==1,"retirement removes all and only descriptors using retired view");
 check(driverSets.count(first)==0&&driverSets.count(sharing)==0,"all affected descriptor sets freed");
 check(!events.empty()&&events.front()=='F',"descriptor invalidation happens before image-view destruction");
 check(renderer.mSamplerDescEpoch==1,"retirement advances draw-cache epoch");
 check(renderer.getSamplerDescriptorSet(otherViews,samplers)==unrelated&&sample(unrelated,0)==77,"unrelated live descriptor remains reusable");
 makeView(11,95); // Same numeric handle, different resource generation and chroma pixels.
 auto replacement=renderer.getSamplerDescriptorSet(views,samplers);
 check(replacement!=first,"recycled view handle cannot retrieve old descriptor");
 check(sample(replacement,0)==42&&sample(replacement,1)==95&&sample(replacement,2)==160,"recycled movie view samples newly written chroma");
 for(int frame=0;frame<60;++frame){
  for(int u=0;u<3;++u)queueVkTextureDelete(100+u,200+u,10+u);
  renderer.flushDeferredDeletes();++renderer.mFrameNumber;
  for(int u=0;u<3;++u)makeView(10+u,frame*3+u);
  auto current=renderer.getSamplerDescriptorSet(views,samplers);
  for(int u=0;u<3;++u)check(sample(current,u)==frame*3+u,"repeated THP frame churn samples current plane pixels");
  check(renderer.mSamplerDescSets.size()==2,"retired movie descriptor sets do not accumulate");
 }
 // An old-device deletion can carry the same numeric handle as a new-device view.
 auto live=renderer.getSamplerDescriptorSet(views,samplers);int deletes=imageDeletes;
 queueVkTextureDelete(999,999,10);++sVkDeviceGeneration;
 renderer.flushDeferredDeletes();
 check(driverViews[10].alive&&imageDeletes==deletes,"old-device deferred deletion does not destroy reused current-device handle");
 check(renderer.getSamplerDescriptorSet(views,samplers)==live&&sample(live,0)==177,"old-device deletion does not invalidate current descriptor");
 queueVkTextureDelete(555,556,0);renderer.flushDeferredDeletes();
 check(renderer.getSamplerDescriptorSet(views,samplers)==live,"viewless retirement does not invalidate unrelated descriptors");
 check(imageDeletes==deletes+1&&memoryDeletes==imageDeletes,"viewless retirement still releases image and memory");
 auto count=renderer.mSamplerDescSets.size();events.clear();renderer.flushDeferredDeletes();
 check(events.empty()&&renderer.mSamplerDescSets.size()==count,"empty retirement flush is harmless");
 printf("%s: %d production descriptor lifetime checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
 return failures?1:0;
}
'''
label = 'before' if a.before else 'fixed'
if a.arm64:
    O = O / 'arm64'
    O.mkdir(exist_ok=True)
cpp = O/(label+'.cpp')
cpp.write_text(fixture, encoding='utf-8')
binary = O/(label if a.arm64 else label+'.exe')
compiler = R/('.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang++.cmd' if a.arm64 else 'build/github-bugfix-2026-10-26/tooling/zig-x86_64-windows-0.15.2/zig.exe')
cmd = [str(compiler)] + ([] if a.arm64 else ['c++']) + ['-std=c++17','-O1',str(cpp),'-o',str(binary)]
if a.arm64:
    cmd += ['-static-libstdc++','-pthread']
else:
    cmd += ['-idirafter',str(vk_include.parent.parent)]
build = subprocess.run(cmd,capture_output=True,text=True)
assert build.returncode==0,build.stdout+build.stderr
report = {'baseline_expected_failure':a.before,'source_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'scope':'Exact production cache/deferred deletion/invalidation methods; fake Vulkan lifetime and handle-reuse driver. No GPU or movie acceptance.'}
if a.arm64:
    report['status']='COMPILED_NOT_EXECUTED'
else:
    run=subprocess.run([str(binary)],capture_output=True,text=True)
    print(run.stdout)
    report.update(exit_code=run.returncode,output=run.stdout)
    assert run.returncode==(1 if a.before else 0),run.stdout+run.stderr
(O/(label+'.json')).write_text(json.dumps(report,indent=2)+'\n')
