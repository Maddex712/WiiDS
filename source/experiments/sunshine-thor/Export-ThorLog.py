"""Export Sunshine's private game log through its own normal SAF action.

Does not change file permissions. Run only after closing its game activity.
"""
from pathlib import Path
import argparse
import re
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('name')
parser.add_argument('--serial', required=True)
args = parser.parse_args()
if not re.fullmatch(r'[a-z0-9-]+', args.name):
    parser.error('Use a lowercase diagnostic name')
adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]


def run(*command):
    return subprocess.check_output(adb + list(command), text=True)


def tap(node):
    bounds = list(map(int, re.findall(r'\d+', node.attrib['bounds'])))
    run('shell', 'input', 'tap', str((bounds[0]+bounds[2])//2),
        str((bounds[1]+bounds[3])//2))


def nodes():
    run('shell', 'uiautomator', 'dump', '/sdcard/sunshine-diagnostic-ui.xml')
    return list(ET.fromstring(run('shell', 'cat', '/sdcard/sunshine-diagnostic-ui.xml')).iter('node'))


if subprocess.run(adb + ['shell', 'pidof', 'com.wither.sms.thor:game'],
                  capture_output=True).returncode == 0:
    raise SystemExit('Close Sunshine gameplay before exporting; no activity changed')
focus = run('shell', 'dumpsys', 'activity', 'activities')
if not any('topResumedActivity' in line and (
        'com.driftdroid.android.thor/' in line or 'com.wither.sms.thor/' in line)
        for line in focus.splitlines()):
    raise SystemExit('Another app is foreground; no activity changed')
run('shell', 'am', 'start', '-n',
    'com.wither.sms.thor/dev.chasem.smslauncher.LauncherActivity')
time.sleep(1)
for attempt in range(3):
    current = nodes()
    if not any(n.get('package') == 'com.wither.sms.thor' for n in current):
        raise SystemExit('Sunshine settings lost foreground')
    export = next((n for n in current if n.get('text') == 'Export game log'), None)
    if export is not None:
        tap(export)
        break
    run('shell', 'input', 'swipe', '470', '850', '470', '330', '300')
else:
    raise SystemExit('Export action not found')
current = nodes()
field = next((n for n in current if n.get('class') == 'android.widget.EditText'
              and n.get('text', '').startswith('sunshine-game.log')), None)
if field is None:
    raise SystemExit('Expected normal Sunshine create-document filename missing')
tap(field)
run('shell', 'input', 'keyevent', '123')
run('shell', 'input', 'keyevent', *(['67'] * len(field.attrib['text'])))
run('shell', 'input', 'text', args.name + '.log')
current = nodes()  # Keyboard can move the Save button.
save = next((n for n in current if n.get('text') == 'SAVE'), None)
if save is None:
    raise SystemExit('SAF Save action missing')
tap(save)
remote = '/sdcard/Download/' + args.name + '.log.txt'
destination = ROOT / 'build/sunshine-thor' / (args.name + '.log')
for attempt in range(5):
    time.sleep(1)
    content = subprocess.run(adb + ['exec-out', 'cat', remote], capture_output=True)
    if content.returncode == 0 and content.stdout:
        destination.write_bytes(content.stdout)
        print(destination)
        break
else:
    raise SystemExit('Export did not produce nonempty expected Downloads file')
