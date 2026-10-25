"""Capture the hint shortly after real first PLAY, before five seconds, and after expiration."""
from pathlib import Path
import argparse,json,subprocess,time
R=Path(__file__).resolve().parents[2];O=R/'build/feature-1.1/nightly-1.1-2026-10-25';O.mkdir(parents=True,exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--pid',required=True);p.add_argument('--label',required=True);a=p.parse_args()
assert a.pid.isdigit() and Path(a.label).name==a.label
adb=[str(R/'.tools/platform-tools/adb.exe'),'-s',a.serial]
log=subprocess.Popen(adb+['logcat','-T','1','--pid='+a.pid,'-v','threadtime','GalaxyLower:I','*:S'],stdout=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
try:
 for line in log.stdout:
  if 'phase=5 ' not in line:continue
  started=time.monotonic();captures=[];print('First playable snapshot detected: '+line.strip(),flush=True)
  for delay in (0.15,4.25,5.75):
   time.sleep(max(0,started+delay-time.monotonic()))
   path=O/(a.label+'-hint-'+str(delay)+'.png')
   with path.open('wb') as f:subprocess.run(adb+['exec-out','screencap','-d','4630946482288158084','-p'],stdout=f,check=True,timeout=20)
   captures.append({'seconds_after_play':time.monotonic()-started,'path':str(path)})
  (O/(a.label+'-hint-captures.json')).write_text(json.dumps({'first_play_log':line.strip(),'captures':captures},indent=2));print('Three timed lower-panel captures saved',flush=True);break
finally:
 log.terminate();log.wait(timeout=10)
