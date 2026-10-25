"""Execute only the dedicated console fixtures while WiiDS is at its library."""
from pathlib import Path
import argparse,json,subprocess
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/galaxy-controls-lighting-2026-10-25/tests'
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);a=p.parse_args()
adb=[str(R/'.tools/platform-tools/adb.exe'),'-s',a.serial]
def call(*parts,check=True):return subprocess.run(adb+list(parts),check=check,capture_output=True,text=True,timeout=45)
foreground=call('shell','dumpsys','activity','activities').stdout
resumed=[s for s in foreground.splitlines() if 'topResumedActivity=' in s]
assert any('ModePickerActivity' in s for s in resumed),resumed
assert not any('GalaxyActivity' in s or '/GameActivity' in s for s in resumed),resumed
remote='/data/local/tmp/wiids-galaxy-controls-tests-20261025'
assert call('shell','test','-e',remote,check=False).returncode!=0,'Refuse to reuse a device test folder'
call('shell','mkdir',remote)
names=['trackpad-camera','camera-permissions-old','camera-permissions-fixed','gamepad-handoff','lighting-generate']
outputs={}
try:
 for name in names:
  call('push',str(O/name),remote+'/'+name);call('shell','chmod','700',remote+'/'+name)
  r=call('shell',remote+'/'+name,*([remote] if name=='lighting-generate' else []),check=False)
  expected=1 if name=='camera-permissions-old' else 0
  assert r.returncode==expected,(name,r.returncode,r.stdout,r.stderr)
  outputs[name]={'returncode':r.returncode,'output':r.stdout.strip()}
  print(name+': '+r.stdout.strip(),flush=True)
 for i in range(36):call('pull',remote+'/lighting-'+str(i)+'.vert',str(O/('lighting-'+str(i)+'.vert')))
finally:
 # Only the exact console-test files created here; never app data or an owner path.
 for name in names+['lighting-'+str(i)+'.vert' for i in range(36)]:call('shell','rm','-f',remote+'/'+name,check=False)
 call('shell','rmdir',remote)
tools=R/'.tools/android-sdk/ndk/29.0.14206865/shader-tools/windows-x86_64'
for i in range(36):
 file=O/('lighting-'+str(i)+'.vert');spv=file.with_suffix('.spv')
 subprocess.run([str(tools/'glslc.exe'),'-fshader-stage=vert',str(file),'-o',str(spv)],check=True)
 subprocess.run([str(tools/'spirv-val.exe'),'--target-env','vulkan1.0',str(spv)],check=True)
report={'status':'PASS','outputs':outputs,'actual_generated_vertex_shaders_compiled_validated':36,'device_console_files_removed':True,
 'scope':'Actual ARM64 policy/mailbox/gamepad execution and real generated GLSL; physical gestures/camera and scene appearance separate'}
(O/'native-result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
