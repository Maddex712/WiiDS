"""Compile real presentation GLSL and execute its color block on flat-color regression inputs.
GPU/edge quality and the Reddit reporter's scene are separate physical acceptance checks.
"""
from pathlib import Path
import hashlib,json,re,subprocess
import numpy as np
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/dolphin-import-2026-10-25/picture-tests';O.mkdir(parents=True,exist_ok=True)
S=R/'reference/galaxy-android/platform/src/gx/vk/vk_presentation_pipeline.cpp'
old=O.parent/'native/before/platform/src/gx/vk/vk_presentation_pipeline.cpp'
tools=R/'.tools/android-sdk/ndk/29.0.14206865/shader-tools/windows-x86_64'
def shaders(path):return dict(re.findall(r'const char\* (\w+) = R"\((.*?)\)";',path.read_text(),re.S))
fixed=shaders(S);baseline=shaders(old)
for name,src in fixed.items():
 stage='vert' if name=='vsSrc' else 'frag';file=O/(name+'.'+stage);file.write_text(src);spv=file.with_suffix('.spv')
 subprocess.run([str(tools/'glslc.exe'),'-fshader-stage='+stage,str(file),'-o',str(spv)],check=True)
 subprocess.run([str(tools/'spirv-val.exe'),'--target-env','vulkan1.0',str(spv)],check=True)
def color_block(shader):
 start=shader.index('    vec3 result = aaCol;') if '    vec3 result = aaCol;' in shader else shader.index('    vec3 bloomSample =')
 text=shader[start:shader.rfind('}')];lines=[];indent=0
 for line in text.splitlines():
  line=line.strip()
  if not line:continue
  if line=='}':indent-=1;continue
  if line=='if (picture.enhanced != 0) {':lines.append(' '*indent+'if enhanced:');indent+=1;continue
  line=re.sub(r'^(vec3|float) ','',line).removesuffix(';')
  assert not any(c in line for c in '{}'),line
  lines.append(' '*indent+line)
 return compile('\n'.join(lines),'<actual shader color block>','exec')
def render(code,color,enhanced):
 rgb=np.array(color,dtype=np.float64)
 env={'aaCol':rgb.copy(),'cN':rgb,'cS':rgb,'cW':rgb,'cE':rgb,'enhanced':enhanced,
  'getLuma':lambda v:float(np.dot(v,[.299,.587,.114])), 'max':max,
  'mix':lambda a,b,t:a*(1-t)+b*t,'vec3':lambda x:np.full(3,x),
  'vec4':lambda x,a:np.r_[x,a],'clamp':lambda x,a,b:np.clip(x,a,b)}
 exec(code,env);return env['outColor'][:3]
neutral=color_block(fixed['fsSrcPost']);legacy=color_block(baseline['fsSrcPost'])
assert np.max(np.abs(render(legacy,[.8]*3,True)-.8))>.02,'Baseline reproduces unwanted brightness change'
count=0
for r in np.linspace(0,1,6):
 for g in np.linspace(0,1,6):
  for b in np.linspace(0,1,6):
   rgb=[r,g,b]
   assert np.max(np.abs(render(neutral,rgb,False)-rgb))<1e-7
   assert np.max(np.abs(render(neutral,rgb,True)-render(legacy,rgb,True)))<1e-7
   count+=1
presenter=(S.parent/'vk_texture_presenter.cpp').read_text();assert 'port_is_post_processing_enabled() == 2 ? 1 : 0' in presenter and 'vkCmdPushConstants(mPresentCommandBuffer, mPresentPipelineLayout' in presenter
result={'status':'PASS','compiled_validated_spirv':len(fixed),'neutral_flat_colors':count,'enhanced_matches_legacy_colors':count,
 'baseline_gray_80_percent':render(legacy,[.8]*3,True).tolist(),'neutral_gray_80_percent':render(neutral,[.8]*3,False).tolist(),
 'source_sha256':hashlib.sha256(S.read_bytes()).hexdigest(),'scope':'Actual GLSL compiled/validated; exact source color block evaluated on CPU flat colors. GPU device boot/edge quality separate.'}
(O/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
