"""Exercise actual generated TEV operand GLSL over all signed 11-bit inputs."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
W = ROOT/'reference/galaxy-android'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, default=W/'platform/src/gx/vk_shadergen_fs.cpp')
p.add_argument('--tables', type=Path, default=W/'platform/src/gx/vk_shadergen_tables.cpp')
p.add_argument('--label', default='fixed')
p.add_argument('--expect-failure', action='store_true')
a = p.parse_args()
assert re.fullmatch(r'[a-z0-9-]+', a.label)
OUT = ROOT/'build/issues-9-10-2026-10-31/implementation/tev-tests'/a.label
OUT.mkdir(parents=True, exist_ok=True)
cpp = OUT/'generate.cpp'
cpp.write_text(r'''
#include "vk_shadergen.h"
#include "gx_shader_uid.h"
#include <fstream>
#include <string>
int main(int argc, char** argv) {
    gpu::ShaderUid u{};
    u.numTexGens=1; u.numChans=1; u.numTevStages=1; u.alphaFunc=63;
    u.tevOrder[0]=(1u<<6); // texture enabled, texcoord/map 0
    // A/B/C use c0, D uses c1; all RGB and alpha lanes are exercised.
    u.tevColor[0]=(2u<<12)|(2u<<8)|(2u<<4)|4u|(1u<<19);
    u.tevAlpha[0]=(1u<<13)|(1u<<10)|(1u<<7)|(2u<<4)|(1u<<19);
    std::ofstream(std::string(argv[1])+"/operand.frag") << gpu::genVulkanFragmentShader(u);
    for (unsigned scale=0;scale<4;scale++) for(unsigned op=0;op<2;op++) {
        u.tevColor[0]=(u.tevColor[0]&~0x340000u)|(scale<<20)|(op<<18);
        u.tevAlpha[0]=(u.tevAlpha[0]&~0x340000u)|(scale<<20)|(op<<18);
        std::ofstream(std::string(argv[1])+"/variant-"+std::to_string(scale)+"-"+std::to_string(op)+".frag") << gpu::genVulkanFragmentShader(u);
    }
}
''', encoding='utf-8')
zig = ROOT/'build/github-bugfix-2026-10-26/tooling/zig-x86_64-windows-0.15.2/zig.exe'
cmd = [str(zig),'c++','-std=c++17','-O1','-I'+str(W/'platform/src/gx'),
    '-I'+str(W/'platform/include'), str(cpp), str(a.source),
    str(a.tables), '-o', str(OUT/'generate.exe')]
build = subprocess.run(cmd, text=True, capture_output=True)
(OUT/'build.log').write_text(build.stdout+build.stderr, encoding='utf-8')
assert build.returncode == 0, 'See build.log'
subprocess.run([str(OUT/'generate.exe'), str(OUT)], check=True)
shader = (OUT/'operand.frag').read_text()
expressions = {k: re.search(r'  tevin_'+k+r' = (.*);', shader)[1] for k in 'abcd'}

class Vec:
    def __init__(self, value): self.value=np.full(4,value,dtype=np.int64)
    def __getattr__(self, name):
        indices=['rgba'.index(c) for c in name]
        result=self.value[indices]
        return int(result[0]) if len(indices)==1 else result

def ivec3(value): return np.full(3,value,dtype=np.int64)
def ivec4(value,alpha=None):
    return np.full(4,value,dtype=np.int64) if alpha is None else np.r_[value,alpha]

checks=0
failures=0
examples=[]
for value in range(-1024,1024):
    env={'c0':Vec(value),'c1':Vec(value),'clamp':np.clip,'ivec3':ivec3,'ivec4':ivec4}
    for key,expression in expressions.items():
        actual=eval(expression,{'__builtins__':{}},env)
        expected=value if key=='d' else value & 255
        checks+=1
        if np.any(actual != expected):
            failures+=1
            if len(examples)<8: examples.append({'operand':key,'value':value,
                'actual':actual.tolist(),'expected':expected})

tools=ROOT/'.tools/android-sdk/ndk/29.0.14206865/shader-tools/windows-x86_64'
shaders=list(OUT.glob('*.frag'))
for shader_file in shaders:
    spv=shader_file.with_suffix('.spv')
    subprocess.run([str(tools/'glslc.exe'),'-fshader-stage=frag',str(shader_file),'-o',str(spv)],check=True)
    subprocess.run([str(tools/'spirv-val.exe'),'--target-env','vulkan1.0',str(spv)],check=True)
report={'status':'FAIL' if failures else 'PASS','checks':checks,'failed_cases':failures,
    'examples':examples,'generated_expressions':expressions,'shaders_compiled_validated':len(shaders),
    'source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),
    'tables_sha256':hashlib.sha256(a.tables.read_bytes()).hexdigest(),
    'reference':'https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/PixelShaderGen.cpp',
    'scope':'Actual C++ shader generator executed on host; generated RGB/alpha operand expressions evaluated across signed 11-bit domain, SPIR-V compilation/validation. Not GPU rendering or reporter-scene acceptance.'}
(OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('examples','generated_expressions')},indent=2))
assert failures>0 if a.expect_failure else failures==0
