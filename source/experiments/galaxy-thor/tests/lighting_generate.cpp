#include "vk_shadergen.h"
#include "gpu.h"
#include "gx_record.h"
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv) {
 if(argc!=2)return 2;
 unsigned count=0;
 for(unsigned source=0;source<4;source++)for(unsigned diffuse=0;diffuse<3;diffuse++)for(unsigned attn: {0u,512u,1536u}) {
  gpu::ShaderUid u{};u.vtxFlags=gpu::VF_NRM|gpu::VF_CLR0|gpu::VF_CLR1;
  u.numChans=2;
  for(unsigned c=0;c<2;c++) {
   u.colorCtrl[c]=2u|60u|(15u<<11)|(source&1u)|((source&2u)<<5)|(diffuse<<7)|attn;
   u.alphaCtrl[c]=u.colorCtrl[c]^(1u|64u); // independently selected alpha sources
  }
  auto text=gpu::genVulkanVertexShader(u);
  char path[1024];snprintf(path,sizeof(path),"%s/lighting-%u.vert",argv[1],count++);
  FILE* f=fopen(path,"wb");if(!f)return 3;fwrite(text.data(),1,text.size(),f);fclose(f);
 }
 printf("%u real vertex shaders generated\n",count);return 0;
}
