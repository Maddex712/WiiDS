"""Bounded lower-screen touch with an upper-screen capture while held; no button injection."""
from pathlib import Path
import argparse, subprocess, time
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--profile', choices=['galaxy','kaizo'], default='galaxy')
p.add_argument('--name', required=True)
p.add_argument('--start', type=int, nargs=2, required=True)
p.add_argument('--end', type=int, nargs=2)
p.add_argument('--display', type=int, choices=[0,4], default=4)
p.add_argument('--confirm', action='store_true', help='Explicit A press while touching (menu selection only)')
a = p.parse_args()
assert Path(a.name).name == a.name
adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', a.serial]
state = subprocess.check_output(adb + ['shell','dumpsys','activity','activities'], text=True)
expected = 'io.github.maddex712.wiids/dev.thor.galaxy.' + ('GalaxyActivity' if a.profile == 'galaxy' else 'KaizoActivities$Game')
assert any('topResumedActivity' in line and expected in line for line in state.splitlines()), 'Selected Galaxy profile must be foreground'
end = a.end or a.start
assert all(0 <= v <= (1919 if a.display == 0 else 1239) for v in (a.start[0], end[0]))
assert all(0 <= v < 1080 for v in (a.start[1], end[1]))
touch = subprocess.Popen(adb + ['shell','input','-d',str(a.display),'swipe',*map(str,a.start),*map(str,end),'2500'])
try:
    time.sleep(.6)
    dest = ROOT / 'build/evidence' / (a.name + '-held-upper.png')
    data = subprocess.check_output(adb + ['exec-out','screencap','-d','4630946441858561667','-p'], timeout=10)
    dest.write_bytes(data)
    with Image.open(dest) as im: im.verify()
    print(dest)
    if a.confirm:
        import sys
        subprocess.run([sys.executable, str(ROOT / 'experiments/release/Control-Thor1_1.py'), '--serial', a.serial,
                        '--target', a.profile, '--keys', 'a', '--ms', '120'], check=True, timeout=10)
finally:
    touch.wait(timeout=10)
