"""Back up existing APKs and Sunshine profiles; compare without writing app data."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/eclipse-thor/before'
PACKAGES = ['io.github.maddex712.wiids', 'com.wither.sms.thor']
PROFILES = {
    'wiids-sunshine': '/sdcard/Android/data/io.github.maddex712.wiids/files/sms',
    'standalone-sunshine': '/sdcard/Android/data/com.wither.sms.thor/files',
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--compare', action='store_true')
    args = p.parse_args()
    adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
    def call(*items): return subprocess.check_output(adb + list(items), text=True, encoding='utf-8').strip()
    current = {}
    for key, remote in PROFILES.items():
        # Fixed constant paths only; never accept caller-controlled shell syntax.
        current[key] = call('shell', f'find {remote}/saves {remote}/game/settings.txt {remote}/controller-mappings.txt -type f 2>/dev/null | sort | while read f; do sha256sum "$f"; done')
    receipt = OUT / 'profiles.json'
    if args.compare:
        before = json.loads(receipt.read_text())
        results = {key: current[key] == before['profiles'][key] for key in current}
        (OUT.parent / 'isolation-comparison.json').write_text(json.dumps({'unchanged': results, 'all_unchanged': all(results.values())}, indent=2) + '\n')
        print(json.dumps(results))
        if not all(results.values()): raise SystemExit('Sunshine profile changed; inspect before claiming isolation')
        return
    if receipt.exists(): raise ValueError('Prior backup retained; do not replace it')
    OUT.mkdir(parents=True, exist_ok=True)
    for key, remote in PROFILES.items():
        (OUT / key).mkdir(parents=True, exist_ok=True)
        for sub in ['saves', 'game/settings.txt', 'controller-mappings.txt']:
            if subprocess.run(adb + ['shell', 'test', '-e', remote + '/' + sub], capture_output=True).returncode == 0:
                (OUT / key / sub).parent.mkdir(parents=True, exist_ok=True)
                subprocess.run(adb + ['pull', remote + '/' + sub, str(OUT / key / sub)], check=True, capture_output=True)
    apks = {}
    for package in PACKAGES:
        remote = call('shell', 'pm', 'path', package)
        paths = [x.removeprefix('package:') for x in remote.splitlines() if x.startswith('package:')]
        if len(paths) != 1: raise ValueError(f'Expected one base APK for {package}')
        local = OUT / (package + '.apk')
        subprocess.run(adb + ['pull', paths[0], str(local)], check=True, capture_output=True)
        apks[package] = {'path': str(local), 'sha256': hashlib.file_digest(local.open('rb'), 'sha256').hexdigest()}
    receipt.write_text(json.dumps({'serial': args.serial, 'profiles': current, 'apks': apks,
                                  'device_data_written': False}, indent=2) + '\n')
    print(receipt)


if __name__ == '__main__': main()
