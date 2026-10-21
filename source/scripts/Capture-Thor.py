"""Capture the Thor's two panels through ADB's binary exec-out channel."""
import argparse
import pathlib
import subprocess
from PIL import Image

root = pathlib.Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('prefix', nargs='?', default='thor')
parser.add_argument('--serial', required=True, help='USB serial or paired wireless ADB endpoint')
args = parser.parse_args()
if args.serial is not None and (not args.serial or any(c.isspace() for c in args.serial)):
    parser.error('--serial must be nonempty without whitespace')
if pathlib.Path(args.prefix).name != args.prefix or args.prefix in ('.', '..'):
    parser.error('prefix must be a filename, not a path')
output = root / 'build/evidence'
output.mkdir(parents=True, exist_ok=True)
adb = str(root / '.tools/platform-tools/adb.exe')
for panel, display in [('upper', '4630946441858561667'), ('lower', '4630946482288158084')]:
    target = output / f'{args.prefix}-{panel}.png'
    pending = target.with_suffix('.pending.png')
    try:
        with pending.open('wb') as stream:
            subprocess.run([adb, '-s', args.serial, 'exec-out', 'screencap', '-d', display, '-p'],
                           stdout=stream, check=True, timeout=20)
        with Image.open(pending) as image:
            image.verify()
        pending.replace(target)
    finally:
        pending.unlink(missing_ok=True)
    print(target)
