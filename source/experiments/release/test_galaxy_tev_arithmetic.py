"""Execute actual generated TEV arithmetic against the GX integer equations.

Owns only private test outputs. Does not alter production sources or access the device.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--expect-failure', action='store_true')
    a = p.parse_args()
    out = a.out.resolve()
    if out == ROOT or ROOT in out.parents:
        p.error('Test outputs must be outside the source tree')
    out.mkdir(parents=True, exist_ok=True)
    runtime = ROOT / 'build/galaxy-integration/source'
    zig = ROOT / 'build/github-bugfix-2026-10-26/tooling/zig-x86_64-windows-0.15.2/zig.exe'
    generator = out / 'generator.cpp'
    generator.write_text(r'''
#include "vk_shadergen.h"
#include "gx_shader_uid.h"
#include <fstream>
#include <iostream>
#include <string>
namespace gpu {std::string vkTevRegular(const char*,uint32_t,uint32_t,uint32_t,bool);}
int main(int argc,char** argv) {
 std::ofstream out(argv[1]);out<<"#include <cstdio>\nstruct V{int a,rgb;};\n";
 int n=0;
 for(int alpha=0;alpha<2;alpha++)for(int bias=0;bias<3;bias++)for(int sub=0;sub<2;sub++)for(int scale=0;scale<4;scale++) {
  auto expression=gpu::vkTevRegular(alpha?"a":"rgb",bias,sub,scale,alpha!=0);
  out<<"int e"<<n<<"(int a,int b,int c,int d){V tevin_a{a,a},tevin_b{b,b},tevin_c{c,c},tevin_d{d,d};return "<<expression<<";}\n";
  std::cout<<n++<<" "<<alpha<<" "<<bias<<" "<<sub<<" "<<scale<<" "<<expression<<"\n";
 }
 out<<"using F=int(*)(int,int,int,int);F functions[]={";for(int i=0;i<n;i++)out<<"e"<<i<<",";out<<"};\n";
 out<<R"(
int reference(int a,int b,int c,int d,int bias,int sub,int scale) {
 int factor=scale==3?1:1<<scale;
 int lerp=(a*256+(b-a)*(c+(c>>7)))*factor;
 if(scale!=3)lerp+=sub?127:128;
 int result=(d+(bias==1?128:bias==2?-128:0))*factor+(sub?-(lerp>>8):(lerp>>8));
 return scale==3?result>>1:result;
}
int main(){unsigned long long checks=0,failed=0;int n=0;int inputs[]={0,1,31,63,64,127,128,201,254,255};int ds[]={-1024,-257,-129,-2,-1,0,1,127,255,511,1023};
 for(int alpha=0;alpha<2;alpha++)for(int bias=0;bias<3;bias++)for(int sub=0;sub<2;sub++)for(int scale=0;scale<4;scale++,n++)
  for(int a:inputs)for(int b:inputs)for(int c:inputs)for(int d:ds){int want=reference(a,b,c,d,bias,sub,scale),got=functions[n](a,b,c,d);checks++;
   if(want!=got){if(failed<8)std::printf("FAIL alpha=%d bias=%d sub=%d scale=%d a=%d b=%d c=%d d=%d got=%d want=%d\n",alpha,bias,sub,scale,a,b,c,d,got,want);failed++;}}
 std::printf("RESULT %llu %llu\n",checks,failed);
}
)";
}''', encoding='utf-8')
    includes = ['-I'+str(runtime/'platform/src/gx'), '-I'+str(runtime/'platform/include')]
    subprocess.run([str(zig), 'c++', '-std=c++20', '-O1', *includes, str(generator), str(a.source), '-o', str(out/'generator.exe')], check=True)
    generated = subprocess.run([str(out/'generator.exe'), str(out/'evaluate.cpp')], check=True, capture_output=True, text=True)
    (out/'generated-expressions.txt').write_text(generated.stdout)
    subprocess.run([str(zig), 'c++', '-std=c++20', '-O2', str(out/'evaluate.cpp'), '-o', str(out/'evaluate.exe')], check=True)
    run = subprocess.run([str(out/'evaluate.exe')], check=True, capture_output=True, text=True)
    (out/'results.txt').write_text(run.stdout)
    result = run.stdout.strip().splitlines()[-1].split()
    checks, failures = map(int, result[1:])
    receipt = {'checks': checks, 'failures': failures, 'source_sha256': hashlib.sha256(a.source.read_bytes()).hexdigest(),
               'reference': 'https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/VideoCommon/PixelShaderGen.cpp',
               'scope': 'Actual generated RGB/alpha equations compiled and executed across biases, add/subtract, all scales, unsigned operands and signed D. Host integer tests; not actor appearance acceptance.'}
    (out/'verification.json').write_text(json.dumps(receipt, indent=2))
    print(run.stdout)
    print(json.dumps(receipt, indent=2))
    assert failures > 0 if a.expect_failure else failures == 0


if __name__ == '__main__':
    main()
