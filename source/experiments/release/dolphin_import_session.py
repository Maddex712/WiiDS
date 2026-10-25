"""Owner save/settings receipt around the authorized Dolphin import preview. Explicit Thor only."""
from pathlib import Path
import argparse,hashlib,json,subprocess
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/dolphin-import-2026-10-25'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);p.add_argument('phase',choices=['before','after']);a=p.parse_args()
adb=[str(R/'.tools/platform-tools/adb.exe'),'-s',a.serial]
source=R/'build/feature-1.1/presentation-fix-2026-10-25/device-after.json' if a.phase=='before' else O/'before.json'
entries=json.loads(source.read_text())['files'];files=[]
for e in entries:
 remote=e['path'];assert remote.startswith('/sdcard/Android/data/io.github.maddex712.wiids/files/')
 b=subprocess.check_output(adb+['exec-out','cat',remote],timeout=20);digest=hashlib.sha256(b).hexdigest()
 files.append({'path':remote,'sha256':digest,'bytes':len(b),'unchanged':digest==e['sha256']})
 if a.phase=='before':
  dest=O/'before-files'/remote.split('/files/',1)[1];assert not dest.exists();dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(b)
result={'phase':a.phase,'checked':len(files),'changed':[f['path'] for f in files if not f['unchanged']],'files':files}
(O/(a.phase+'.json')).write_text(json.dumps(result,indent=2));print(json.dumps({k:result[k] for k in ('phase','checked','changed')}))
