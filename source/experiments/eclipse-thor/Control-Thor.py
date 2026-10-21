"""Bounded test input through the identified physical Odin device; always releases."""
from pathlib import Path
import argparse
import subprocess

ROOT = Path(__file__).resolve().parents[2]
KEYS = {'a': 304, 'b': 305, 'x': 307, 'y': 308, 'start': 315, 'l': 310, 'r': 311,
        'up': 544, 'down': 545, 'left': 546, 'right': 547}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--key', choices=KEYS)
    p.add_argument('--axis', type=int, choices=[0, 1])
    p.add_argument('--value', type=int, default=0)
    p.add_argument('--ms', type=int, default=120)
    a = p.parse_args()
    if not 30 <= a.ms <= 2000 or abs(a.value) > 32767 or (a.key is None) == (a.axis is None):
        p.error('Choose one key or axis, 30..2000 milliseconds, and a valid axis value')
    adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', a.serial]
    state = subprocess.check_output(adb + ['shell', 'dumpsys', 'activity', 'activities'], text=True)
    if not any('topResumedActivity' in line and 'com.wither.sms.eclipse.thor/' in line for line in state.splitlines()):
        raise ValueError('Eclipse is not foreground; input refused')
    devices = subprocess.check_output(adb + ['shell', 'getevent', '-pl'], text=True)
    block = next((b for b in devices.split('add device ') if '"Odin Controller"' in b), None)
    if not block: raise ValueError('Physical Odin controller is missing')
    device = block.splitlines()[0].split(': ', 1)[1]
    if not device.startswith('/dev/input/event') or not device.removeprefix('/dev/input/event').isdigit():
        raise ValueError('Invalid kernel input path')
    kind, code, value = (1, KEYS[a.key], 1) if a.key else (3, a.axis, a.value)
    release = f'sendevent {device} {kind} {code} 0; sendevent {device} 0 0 0'
    script = f"trap '{release}' EXIT; sendevent {device} {kind} {code} {value}; sendevent {device} 0 0 0; sleep {a.ms / 1000:.3f}"
    subprocess.run(adb + ['shell', script], check=True, timeout=10)
    print(f'Released {a.key or "axis " + str(a.axis)} on {device}')


if __name__ == '__main__': main()
