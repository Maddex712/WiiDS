"""Execute the actual emitted GX material/ambient expressions on numeric fixtures.
Full shader compilation and scene rendering are separate checks.
"""
from pathlib import Path
import json,re
import numpy as np
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/galaxy-controls-lighting-2026-10-25/tests'
relative='reference/galaxy-android/platform/src/gx/vk_shadergen_vs.cpp'
old=O.parent/'before'/relative;current=R/relative
def statements(path):
    strings=[json.loads('"'+s+'"') for s in re.findall(r'o\.w\("((?:[^"\\]|\\.)*)"',path.read_text())]
    return [next(s for s in strings if '%s lit =' in s),next(s for s in strings if 'lit +=' in s),
            next(s for s in strings if 'v_clr%d.%s = ' in s and 'clamp(lit' in s)]
def shade(path,mat,amb,lights):
    start,add,end=statements(path);dim='float' if np.ndim(mat)==0 else 'vec3';sw='a' if dim=='float' else 'rgb'
    env={'mat':np.array(mat),'amb':np.array(amb),'clamp':np.clip,'float':float,'vec3':lambda v:np.full(3,v)}
    def run(s):
        s=re.sub(r'\b(vec3|float) (\w+) =',r'\2 =',s).replace('{','').replace('}','').strip().removesuffix(';')
        exec(s,env)
    run(start%(dim,'amb'))
    for color,intensity in lights:
        env.update(light=np.array(color),illum=intensity,a=1)
        s=add%('mat',sw) if add.count('%s')==2 else add%sw
        run(s.replace('fetch_st(b).'+sw,'light'))
    s=end%(0,sw,dim,dim,'mat') if end.count('%s')==4 else end%(0,sw,dim,dim)
    run(s.replace('v_clr0.'+sw,'result'));return env['result']
def expected(mat,amb,lights):
    return np.array(mat)*np.clip(np.array(amb)+sum((np.array(c)*i for c,i in lights),start=np.zeros_like(np.array(amb))),0,1)
fixtures=[([.2,.4,.8],[.6,.3,.2],[]),([.25,.5,.75],[.2,.2,.2],[([1,1,1],2)]),
          ([0,.2,1],[1,.8,.2],[([1,0,0],1)]),([.6,.5,.4],[.8,.8,.8],[([1,.5,0],-.5)]),
          (.3,.8,[]),(.4,.1,[(1.,2.)])]
old_failures=int(sum(np.max(np.abs(shade(old,*f)-expected(*f)))>1e-7 for f in fixtures))
assert old_failures>=4,'Fixtures must reproduce the old material/ambient error'
failures=int(sum(np.max(np.abs(shade(current,*f)-expected(*f)))>1e-7 for f in fixtures))
print('Old source:',old_failures,'reproduced failures; current:',failures)
assert failures==0,'Actual emitted lighting expressions violate GX material/ambient modulation'
n=0
for mat in np.linspace(0,1,9):
 for amb in np.linspace(0,1,9):
  for intensity in (-1,-.25,0,.5,2):
   f=(mat,amb,[(1.,intensity)])
   assert abs(shade(current,*f)-expected(*f))<1e-7;n+=1
result={'status':'PASS','old_fixture_failures':old_failures,'fixtures':len(fixtures),'scalar_combinations':n,
        'scope':'Actual emitted GLSL material/ambient expressions evaluated; not a device rendering claim',
        'reference':'https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/LightingShaderGen.cpp'}
(O/'lighting-result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
