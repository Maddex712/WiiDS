"""Recompile MSMBWii's actual overlay unit and relink its existing ARM64 graph."""
from pathlib import Path
import hashlib,json,subprocess
R=Path(__file__).resolve().parents[2]
O=R/'build/feature-1.1/thor-polish-2026-10-24/native-fixed/msmbwii'
O.mkdir(parents=True,exist_ok=True)
B=R/'build/nsmbw-thor/msmbwii/android-native'
T=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'
graph=json.loads(subprocess.check_output([str(R/'.tools/host-tools/bin/ninja.exe'),'-C',str(B),'-t','compdb','-x'],text=True))
name='unity_runtime_4_cxx.cxx'
entry=next(e for e in graph if e['output'].endswith('/'+name+'.o'))
original=B/'CMakeFiles/mkw_runtime_common.dir/Unity'/name
copied=O/name;copied.write_text(original.read_text())
obj=O/(name+'.o')
command=entry['command'].replace(original.as_posix(),copied.as_posix())
assert command!=entry['command']
command=command.replace(entry['output']+'.d',obj.as_posix()+'.d').replace(entry['output'],obj.as_posix())
with (O/'compile.log').open('w') as log:
    p=subprocess.run(command,cwd=B,stdout=log,stderr=subprocess.STDOUT)
assert p.returncode==0,'Overlay compile failed'
link=next(e['command'] for e in graph if e['output']=='libmsmbwii.so')
link=next(s for s in link.split(' && ') if 'clang++.exe ' in s and ' -o libmsmbwii.so' in s)
assert 'rcheevos' not in link and entry['output'] in link
link=link.replace(entry['output'],obj.as_posix()).replace(' -o libmsmbwii.so ',f' -o {(O/"libmsmbwii.so").as_posix()} ')
with (O/'link.log').open('w') as log:
    p=subprocess.run(link,cwd=B,stdout=log,stderr=subprocess.STDOUT)
assert p.returncode==0,'Overlay link failed'
subprocess.run([str(T/'llvm-strip.exe'),'--strip-unneeded',str(O/'libmsmbwii.so')],check=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
(O/'commands.txt').write_text(command+'\n'+link+'\n')
(O/'verification.json').write_text(json.dumps({'status':'PASS','libmsmbwii_sha256':sha(O/'libmsmbwii.so'),
 'overlay_source_sha256':sha(R/'reference/nsmbw-android-msmbwii/runtime/src/settings_overlay.cpp'),
 'scope':'Actual ARM64 compile/link of overlay lifetime check; physical gameplay acceptance separate'},indent=2)+'\n')
print('PASS actual MSMBWii overlay ARM64 compile/link')
