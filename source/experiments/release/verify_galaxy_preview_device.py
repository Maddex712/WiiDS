"""Readback only: verify the explicit preview APK and the fresh external-file baseline."""
from pathlib import Path
import argparse,hashlib,json,subprocess
R=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--candidate',type=Path,required=True);p.add_argument('--phase',required=True);a=p.parse_args()
assert Path(a.phase).name==a.phase
base=json.loads(a.baseline.read_text());candidate=json.loads(a.candidate.read_text())
adb=[str(R/'.tools/platform-tools/adb.exe'),'-s',a.serial]
def call(*parts):return subprocess.check_output(adb+list(parts),timeout=45)
apk=call('shell','pm','path',base['package']).decode().strip().removeprefix('package:')
sha=call('shell','sha256sum',apk).decode().split()[0];assert sha==candidate['sha256'],sha
package=call('shell','dumpsys','package',base['package']).decode();assert 'versionName='+candidate['version_name'] in package
records=[]
for file in base['files']:
 if not file['readable']:continue
 data=call('exec-out','cat',file['path']);digest=hashlib.sha256(data).hexdigest()
 records.append({'path':file['path'],'sha256':digest,'bytes':len(data),'unchanged':digest==file['sha256']})
changed=[f['path'] for f in records if not f['unchanged']]
report={'status':'PASS' if not changed else 'CHANGED','version':candidate['version_name'],'installed_sha256':sha,
 'checked':len(records),'changed':changed,'files':records,'scope':'Accessible external files only; no all-internal-data claim'}
(a.baseline.parent/(a.phase+'.json')).write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='files'}));assert not changed,changed
