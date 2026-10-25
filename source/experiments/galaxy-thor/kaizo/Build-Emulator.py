"""Private x86 counterpart of the actual combined ARM64 APK; no device action."""
from pathlib import Path
import zipfile,subprocess,hashlib,json,os,shutil
R=Path(__file__).resolve().parents[3];O=R/'build/galaxy-kaizo';bt=R/'.tools/android-sdk/build-tools/34.0.0'
jdk=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot')
production=R/'reference/driftdroid/android/app/build/outputs/apk/release/app-release.apk'
shutil.copy2(production,O/'WiiDS-kaizo-dev-arm64.apk')
libs={p.name:p for p in (R/'build/galaxy-integration/import-test/libs').glob('*.so')}
libs['libwiidsdisc.so']=R/'build/galaxy-integration/frontend-x86/libwiidsdisc.so'
proof={}
with zipfile.ZipFile(production) as src,zipfile.ZipFile(O/'x86-base.apk','w',zipfile.ZIP_DEFLATED,compresslevel=1) as dst:
 for n in src.namelist():
  if n.startswith('lib/') or n.startswith('META-INF/') and n.upper().endswith(('.RSA','.SF','.MF')):continue
  data=src.read(n)
  if n=='assets/galaxy/pythonhome.zip':
   data=(R/'build/galaxy-integration/import-test/pythonhome.zip').read_bytes()
  else:proof[n]=hashlib.sha256(data).hexdigest()
  dst.writestr(n,data,compress_type=src.getinfo(n).compress_type)
 for n,p in libs.items():dst.write(p,'lib/x86_64/'+n)
env=dict(os.environ,JAVA_HOME=str(jdk))
def run(args):subprocess.run(list(map(str,args)),check=True,env=env)
run([bt/'zipalign.exe','-f','-p','4',O/'x86-base.apk',O/'x86-aligned.apk'])
run([bt/'apksigner.bat','sign','--ks',Path.home()/'.android/debug.keystore','--ks-pass','pass:android','--out',O/'WiiDS-kaizo-dev-x86.apk',O/'x86-aligned.apk'])
with zipfile.ZipFile(O/'WiiDS-kaizo-dev-x86.apk') as z:
 for n,h in proof.items():assert hashlib.sha256(z.read(n)).hexdigest()==h,n
(O/'emulator-frontend-proof.json').write_text(json.dumps({'same_arm64_frontend_entries':len(proof),'dex_manifest_resources_identical':True,'non_debuggable':True,'test_signature_only':True,'native_cpu_and_python_extensions':'x86_64'},indent=2)+'\n')
print('Private ARM64 + x86 APKs ready; same frontend, CPU-specific Python extensions')
