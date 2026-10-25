"""Exercise the actual emitted signed-diffuse expression, including rim alpha.

GX_DF_SIGN must retain negative contributions until the accumulated light is
clamped. No disc data is needed. Full shader/device tests remain separate.
"""
from pathlib import Path
import argparse, json, math, re

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, default=ROOT/'reference/galaxy-android/platform/src/gx/vk_shadergen_vs.cpp')
p.add_argument('--report', type=Path)
args = p.parse_args()
text = args.source.read_text(encoding='utf-8')
branch = re.search(r'else if \(diffuse == 1\) \{\s*(?://[^\n]*\n\s*)*o\.w\("((?:[^"\\]|\\.)*)"', text)
assert branch, 'Find the actual signed-diffuse generator branch'
statement = json.loads('"'+branch[1]+'"')
expression = re.fullmatch(r'\s*float illum = (.*);\s*', statement)[1]
checks = 0
failures = []
for normal_dot in (-1., -.75, -.5, -.25, 0., .25, .5, .75, 1.):
    # Unit vectors with the requested dot; avoids assuming normalized dot > 0.
    nrm = (math.sqrt(max(0., 1.-normal_dot*normal_dot)), 0., normal_dot)
    light = (0., 0., 1.)
    got = eval(expression, {'__builtins__': {}, 'max': max,
               'dot': lambda a,b: sum(x*y for x,y in zip(a,b)),
               'nrm': nrm, 'lvec': light})
    for ambient in (0., 75./255., .5, 1.):
        for color in (.25, 165./255., 1.):
            checks += 1
            actual = min(1., max(0., ambient+color*got))
            expected = min(1., max(0., ambient+color*normal_dot))
            if abs(actual-expected)>1e-7:
                failures.append({'dot':normal_dot,'ambient':ambient,'light':color,
                                 'actual':actual,'expected':expected})
# The corrective change must not replace the game's clamped diffuse mode.
assert 'float illum = clamp((dot(nrm, lvec) >= 0.0 ? dot(nrm, lvec) : 0.0), 0.0, 1.0);' in text
assert 'float illum = 1.0;' in text
result = {'status':'PASS' if not failures else 'FAIL', 'checks':checks,
          'failures':failures, 'emitted_signed_expression':expression,
          'scope':'Actual generated signed-diffuse expression; accumulated RGB/alpha light, not a scene-rendering claim',
          'reference':'https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/LightingShaderGen.cpp'}
if args.report:
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='failures'}))
print('Failed cases:',len(failures))
raise SystemExit(bool(failures))
