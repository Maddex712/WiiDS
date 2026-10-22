"""Read back the final local packages/profile and preserve bounded receipts."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'build/galaxy-thor'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
args = parser.parse_args()
adb = [str(root / '.tools/platform-tools/adb.exe'), '-s', args.serial]
def read(*command):
    return subprocess.check_output(adb + list(command), text=True, encoding='utf-8', errors='replace')

packages = []
for package, expected, debug, version in (
    ('io.github.maddex712.wiids', json.loads((out / 'wiids-device-install.json').read_text())['sha256'], False, 906),
    ('io.github.maddex712.wiids.galaxy', json.loads((out / 'runtime-apk/package.json').read_text())['sha256'], True, 2)):
    installed = read('shell', 'pm', 'path', package).strip().removeprefix('package:')
    assert installed.startswith('/data/app/') and '\n' not in installed
    actual = read('shell', 'sha256sum', installed).split()[0]
    assert actual == expected, package + ': installed APK mismatch'
    details = read('shell', 'dumpsys', 'package', package)
    assert f'versionCode={version} ' in details
    flags = re.search(r'^\s*flags=\[([^\]]*)\]', details, re.M).group(1).split()
    assert ('DEBUGGABLE' in flags) == debug
    packages.append({'package': package, 'versionCode': version, 'debuggable': debug,
                     'installedSha256': actual, 'hashMatched': True})

ready = read('shell', 'content', 'query', '--uri', 'content://io.github.maddex712.wiids.galaxy.status/ready')
assert 'ready=1' in ready
focus = read('shell', 'dumpsys', 'activity', 'activities')
assert re.search(r'topResumedActivity=.*io.github.maddex712.wiids/com.driftdroid.android.ModePickerActivity', focus)
game = subprocess.run(adb + ['shell', 'pidof', 'io.github.maddex712.wiids.galaxy:game'], capture_output=True)
assert game.returncode == 1 and not game.stdout.strip()

art_root = '/sdcard/Android/data/io.github.maddex712.wiids/files/custom/launcher/'
manifest_bytes = subprocess.check_output(adb + ['exec-out', 'cat', art_root + 'artwork.json'])
art = json.loads(manifest_bytes)
prior_art = json.loads((out / 'launcher-artwork-before.json').read_bytes())
for key, value in prior_art.items():
    if key != 'images':
        assert art[key] == value
for name, entry in prior_art['images'].items():
    assert art['images'][name] == entry
for entry in art['images'].values():
    assert Path(entry['file']).name == entry['file']
    assert read('shell', 'sha256sum', art_root + entry['file']).split()[0] == entry['sha256']

save_records = []
for remote in read('shell', 'run-as', 'io.github.maddex712.wiids.galaxy', 'find', 'files/nand', '-type', 'f').splitlines():
    assert remote.startswith('files/nand/') and '..' not in remote.split('/')
    data = subprocess.check_output(adb + ['exec-out', 'run-as', 'io.github.maddex712.wiids.galaxy', 'cat', remote])
    target = out / 'galaxy-starter-backup' / remote
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    save_records.append({'file': remote, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})

log = read('logcat', '-d', '--pid=4511', '-v', 'time')
(out / 'final-gameplay-logcat.txt').write_text(log, encoding='utf-8')
assert 'stage=PeachCastleGardenGalaxy' in log
assert 'FATAL NATIVE CRASH' not in log and 'Fatal signal' not in log
receipt = {'result': 'PASS', 'packages': packages, 'ready': True,
    'leftAtWiiDSLibrary': True, 'gameProcessExited': True,
    'existingArtworkUnchanged': len(prior_art['images']), 'allArtworkHashesMatched': len(art['images']),
    'priorSaveVerification': json.loads((out / 'prior-saves-verification.json').read_text()),
    'galaxyStarterBackup': save_records,
    'liveChecks': ['WiiDS Galaxy case launch', 'animated title and A+B', 'right-stick pointer',
                   'normal file creation and fresh-process reload', 'opening story and Star Festival',
                   'left-stick movement', 'A jump', 'Start pause/resume', 'Back returns to WiiDS'],
    'scopedGamePid': 4511,
    'limits': ['No full-stage or all-game compatibility claim', 'No sustained FPS/audio/haptic validation',
               'Lower display is a controls guide, not a live HUD',
               'Isolated development helper requires debuggable preloader',
               'Upstream gear/SAVE overlay and Android gesture indicator remain visible']}
(out / 'device-verification.json').write_text(json.dumps(receipt, indent=2))
print('PASS: both installed APKs, readiness, prior data/art, gameplay log and library exit')
