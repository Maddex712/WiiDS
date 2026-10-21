"""Capture only the selected Sunshine/Thor library upper screen while foreground."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
UPPER_DISPLAY = '4630946441858561667'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('name')
    parser.add_argument('--serial', required=True)
    parser.add_argument('--launcher', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z0-9-]+', args.name):
        parser.error('Use a simple lowercase evidence name')
    adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
    package = 'com.driftdroid.android.thor' if args.launcher else 'com.wither.sms.thor'
    focus = subprocess.check_output(adb + ['shell', 'dumpsys', 'activity', 'activities'], text=True)
    focused = [line.strip() for line in focus.splitlines()
               if 'topResumedActivity' in line and package + '/' in line]
    if not focused:
        raise ValueError('Requested app is not foreground; no screen capture performed')
    folder = ROOT / 'build/sunshine-thor/evidence'
    folder.mkdir(exist_ok=True)
    pixels = subprocess.check_output(adb + ['exec-out', 'screencap', '-d', UPPER_DISPLAY, '-p'])
    if not pixels.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ValueError('Screen capture did not return a PNG')
    target = folder / (args.name + '.png')
    target.write_bytes(pixels)
    with Image.open(target) as image:
        size = list(image.size)
        image.thumbnail((960, 640))
        image.save(folder / (args.name + '-preview.png'))
    (folder / (args.name + '.json')).write_text(json.dumps({
        'package': package, 'foreground': focused, 'physical_display': UPPER_DISPLAY,
        'dimensions': size, 'sha256': hashlib.sha256(pixels).hexdigest(),
    }, indent=2) + '\n')
    print(target)


if __name__ == '__main__':
    main()
