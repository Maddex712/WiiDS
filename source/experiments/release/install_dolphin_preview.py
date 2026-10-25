"""Install the verified owner-signed preview in place, only while no owner game is active."""
from pathlib import Path
import argparse,hashlib,json,subprocess
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/dolphin-import-2026-10-25'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);a=p.parse_args()
adb=[str(R/'.tools/platform-tools/adb.exe'),'-s',a.serial];pkg='io.github.maddex712.wiids'
receipt=json.loads((O/'preview17/candidate.json').read_text());apk=Path(receipt['apk'])
assert receipt['status']=='PASS' and receipt['version_name']=='1.1-dev.17' and receipt['version_code']==906
assert hashlib.sha256(apk.read_bytes()).hexdigest()==receipt['sha256']
assert json.loads((O/'preview17/audit.json').read_text())['status']=='pass'
assert not json.loads((O/'before.json').read_text())['changed']
def idle():
 state=subprocess.check_output(adb+['shell','dumpsys','activity','activities'],text=True)
 active=[s for s in state.splitlines() if 'topResumedActivity=' in s]
 assert all(any(x in s for x in ('rip.moth.cocoonshell/',pkg+'/com.driftdroid.android.ModePickerActivity','io.github.maddex712.wiids.dolphintests/')) for s in active),'Owner activity foreground; abort'
 names=subprocess.check_output(adb+['shell','ps','-A','-o','NAME'],text=True).splitlines()
 assert not any(n.startswith(pkg+':') and (n.endswith('_game') or n.endswith('_import')) for n in names),'Embedded game/importer exists; abort'
idle()
installed=subprocess.run(adb+['install','-r',str(apk)],capture_output=True,text=True);installed.check_returncode();assert 'Success' in installed.stdout
path=subprocess.check_output(adb+['shell','pm','path',pkg],text=True).strip().removeprefix('package:')
assert path.startswith('/data/app/') and path.endswith('/base.apk') and '\n' not in path
digest=subprocess.check_output(adb+['shell','sha256sum',path],text=True).split()[0];assert digest==receipt['sha256']
subprocess.run(adb+['shell','am','start','-n',pkg+'/com.driftdroid.android.ModePickerActivity'],check=True,capture_output=True)
result={'status':'PASS','version':receipt['version_name'],'sha256':digest,'installed_in_place':True,'published':False,'owner_save_receipt':'before.json; final readback pending'}
(O/'install.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
