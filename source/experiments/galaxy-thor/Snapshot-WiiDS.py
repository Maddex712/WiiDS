"""Back up bounded existing saves/settings; later compare without restoring them."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = '/sdcard/Android/data/io.github.maddex712.wiids/files/'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
parser.add_argument('--verify', action='store_true')
args = parser.parse_args()
adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
out = ROOT / 'build/galaxy-thor/device-before'
out.mkdir(parents=True, exist_ok=True)
metadata = out / 'files.json'
if not args.verify:
    paths = []
    for folder in ('sms/saves', 'eclipse/saves', 'nsmbw/NAND/title/00010000/534d4e45/data'):
        found = subprocess.run(adb + ['shell', 'find', BASE + folder, '-type', 'f'],
                               capture_output=True, text=True, check=True)
        paths.extend(found.stdout.splitlines())
    for path in ('sms/game/settings.txt', 'eclipse/game/settings.txt',
                 'sms/controller-mappings.txt', 'eclipse/controller-mappings.txt',
                 'nsmbw/Config.toml', 'msmbwii/Config.toml'):
        paths.append(BASE + path)
    records = []
    for remote in paths:
        if not remote.startswith(BASE) or '..' in remote[len(BASE):].split('/'):
            raise ValueError('Unexpected backup path')
        relative = remote[len(BASE):]
        local = out / relative
        local.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(adb + ['pull', remote, str(local)], check=True, capture_output=True)
        value = hashlib.sha256(local.read_bytes()).hexdigest()
        remote_hash = subprocess.run(adb + ['shell', 'sha256sum', remote],
                                     check=True, capture_output=True, text=True).stdout.split()[0]
        assert value == remote_hash
        records.append({'file': relative, 'sha256': value})
    metadata.write_text(json.dumps(records, indent=2))
else:
    records = json.loads(metadata.read_text())
    for record in records:
        value = subprocess.run(adb + ['shell', 'sha256sum', BASE + record['file']],
                                check=True, capture_output=True, text=True).stdout.split()[0]
        assert value == record['sha256'], f"Changed save/settings: {record['file']}"
    (ROOT / 'build/galaxy-thor/prior-saves-verification.json').write_text(
        json.dumps({'filesUnchanged': len(records), 'files': records, 'result': 'PASS'}, indent=2))
print('PASS:', len(records), 'save/settings files')
