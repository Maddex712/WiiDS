"""Read-only save/settings receipt for the owner-authorized Galaxy pointer pass."""
from pathlib import Path
import argparse, hashlib, json, subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/feature-1.1/galaxy-pointer-2026-10-25'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('phase', choices=['before', 'after'])
a = p.parse_args()
OUT.mkdir(parents=True, exist_ok=True)
adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', a.serial]
source = ROOT / 'build/feature-1.1/thor-polish-2026-10-24/device-after.json' if a.phase == 'before' else OUT / 'before.json'
entries = json.loads(source.read_text())['files']
files = []
for entry in entries:
    remote = entry['path']
    assert remote.startswith('/sdcard/Android/data/io.github.maddex712.wiids/files/')
    data = subprocess.check_output(adb + ['exec-out', 'cat', remote], timeout=20)
    digest = hashlib.sha256(data).hexdigest()
    files.append({'path': remote, 'sha256': digest, 'bytes': len(data), 'unchanged': digest == entry['sha256']})
    if a.phase == 'before':
        dest = OUT / 'before-files' / remote.split('/files/', 1)[1]
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
result = {'checked': len(files), 'changed': [f['path'] for f in files if not f['unchanged']], 'files': files}
(OUT / (a.phase + '.json')).write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({'phase': a.phase, 'checked': len(files), 'changed': result['changed']}))
