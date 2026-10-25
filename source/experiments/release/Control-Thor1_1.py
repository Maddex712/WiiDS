"""Bounded, foreground-guarded Odin input for the Galaxy bring-up; always releases."""
from pathlib import Path
import argparse
import subprocess

ROOT = Path(__file__).resolve().parents[2]
KEYS = {'a': 304, 'b': 305, 'x': 307, 'y': 308, 'start': 315,
        'l': 310, 'r': 311, 'l2': 312, 'r2': 313, 'r3': 318,
        'up': 544, 'down': 545, 'left': 546, 'right': 547, 'back': 158}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
parser.add_argument('--target', choices=('galaxy', 'kaizo', 'library', 'nsmb', 'msmb', 'mkw', 'save-manager', 'achievements', 'cocoon'), default='galaxy')
parser.add_argument('--keys', help='Comma-separated simultaneous keys')
parser.add_argument('--axis', type=int, choices=(0, 1, 2, 5, 9, 10))
parser.add_argument('--value', type=int, default=0)
parser.add_argument('--ms', type=int, default=120)
args = parser.parse_args()
if not 30 <= args.ms <= 2000 or abs(args.value) > 32767 or (args.keys is None) == (args.axis is None):
    parser.error('Choose keys or one axis, with a bounded hold')
keys = args.keys.split(',') if args.keys else []
if any(k not in KEYS for k in keys):
    parser.error('Unknown key')
adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
state = subprocess.check_output(adb + ['shell', 'dumpsys', 'activity', 'activities'], text=True)
expected = {'galaxy': 'io.github.maddex712.wiids/dev.thor.galaxy.GalaxyActivity',
            'kaizo': 'io.github.maddex712.wiids/dev.thor.galaxy.KaizoActivities$Game',
            'library': 'io.github.maddex712.wiids/com.driftdroid.android.ModePickerActivity',
            'nsmb': 'io.github.maddex712.wiids/com.wither.nsmbw.GameActivity',
            'msmb': 'io.github.maddex712.wiids/com.wither.nsmbw.MsmbwiiActivities$Game',
            'mkw': 'io.github.maddex712.wiids/com.driftdroid.android.MainActivity',
            'save-manager': 'io.github.maddex712.wiids/com.driftdroid.android.SaveManagerActivity',
            'achievements': 'io.github.maddex712.wiids/com.driftdroid.android.achievements.AchievementsActivity',
            'cocoon': 'rip.moth.cocoonshell/.MainActivity'}[args.target]
if not any('topResumedActivity' in line and expected in line for line in state.splitlines()):
    raise SystemExit('Expected game/library is not foreground; input refused')
inventory = subprocess.check_output(adb + ['shell', 'getevent', '-pl'], text=True)
block = next((b for b in inventory.split('add device ') if '"Odin Controller"' in b), None)
if block is None:
    raise SystemExit('Odin controller not found')
device = block.splitlines()[0].split(': ', 1)[1]
if not device.startswith('/dev/input/event') or not device.removeprefix('/dev/input/event').isdigit():
    raise SystemExit('Unexpected input path')
events = [(1, KEYS[k], 1) for k in keys] if keys else [(3, args.axis, args.value)]
release = '; '.join(f'sendevent {device} {kind} {code} 0' for kind, code, _ in events)
release += f'; sendevent {device} 0 0 0'
press = '; '.join(f'sendevent {device} {kind} {code} {value}' for kind, code, value in events)
script = f"trap '{release}' EXIT; {press}; sendevent {device} 0 0 0; sleep {args.ms / 1000:.3f}"
subprocess.run(adb + ['shell', script], check=True, timeout=10)
print('Released', args.keys or f'axis {args.axis}', 'on', device)
