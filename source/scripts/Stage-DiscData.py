"""Stream the user's validated local extraction into our isolated debug app profile."""
import argparse
import json
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parent.parent
adb = root / '.tools/platform-tools/adb.exe'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True, help='Explicit USB serial or paired ADB endpoint')
args = parser.parse_args()
if args.serial is not None and (not args.serial or any(c.isspace() for c in args.serial)):
    parser.error('--serial must be nonempty without whitespace')
serial = args.serial
package = 'com.driftdroid.android.thor'
validation = json.loads((root / 'build/disc-validation.json').read_text(encoding='utf-8-sig'))
if validation.get('gameId') != 'RMCP01':
    raise SystemExit('No validated PAL extraction is available.')

archive = root / 'build/discdata.tar'
subprocess.run(['tar', '--format=ustar', '-cf', str(archive), '-C', str(root / 'reference/driftdroid/DiscData'),
                'files', 'sys'], check=True)
command = [str(adb), '-s', serial]
subprocess.run(command + ['shell', 'run-as', package, 'mkdir', '-p', 'files/WiiCompiled/DiscData'], check=True)
with archive.open('rb') as data:
    # exec-in sets Windows adb's stdin to binary; `shell -T` truncates at Ctrl-Z.
    subprocess.run(command + ['exec-in', 'run-as', package, 'tar', '-xf', '-', '-C',
                              'files/WiiCompiled/DiscData'], stdin=data, check=True)

for relative, key in [('sys/main.dol', 'dolSha256'), ('files/rel/StaticR.rel', 'relSha256')]:
    result = subprocess.check_output(command + ['shell', 'run-as', package, 'sha256sum',
                                               f'files/WiiCompiled/DiscData/{relative}'], text=True)
    if result.split()[0].lower() != validation[key].lower():
        raise SystemExit(f'Device hash mismatch: {relative}')
print('Validated game data staged in the isolated Thor debug profile; device executable hashes match.')
