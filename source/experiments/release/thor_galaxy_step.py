"""One bounded input into a known test PID; abort if its foreground changes."""
from pathlib import Path
import argparse,subprocess
R=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial',required=True);p.add_argument('--pid',required=True)
p.add_argument('--game',choices=['kaizo','galaxy'],default='kaizo')
p.add_argument('--duration',type=int,default=220)
p.add_argument('--axes',type=float,nargs=4,default=[0,0,0,0])
p.add_argument('--key',type=int,action='append',default=[])
p.add_argument('--swipe',type=int,nargs=4)
a=p.parse_args();adb=[str(R/'.tools/platform-tools/adb.exe'),'-s',a.serial]
def read(*args):return subprocess.check_output(adb+list(args),timeout=15).decode(errors='replace').strip()
process='galaxy_kaizo_game' if a.game=='kaizo' else 'galaxy_game'
activity='KaizoActivities$Game' if a.game=='kaizo' else 'GalaxyActivity'
assert read('shell','pidof','io.github.maddex712.wiids:'+process)==a.pid,'Test process changed: stop input'
top='\n'.join(x for x in read('shell','dumpsys','activity','activities').splitlines() if 'topResumedActivity=' in x)
assert 'io.github.maddex712.wiids/dev.thor.galaxy.'+activity in top,'Test no longer foreground: stop input'
assert 1<=a.duration<=5000 and all(abs(v)<=1 for v in a.axes)
if a.swipe:
 cmd=['shell','input','-d','4','swipe',*map(str,a.swipe),str(a.duration)]
else:
 values=[str(a.duration),*map(str,a.axes),*map(str,a.key)]
 cmd=['shell','CLASSPATH=/data/local/tmp/wiids-attack-input.dex app_process /system/bin ShellInput '+' '.join(values)]
subprocess.run(adb+cmd,check=True,timeout=15)
