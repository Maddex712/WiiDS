"""Stage the verified owner disc into Sunshine's own app files; never launches it."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
REMOTE_ROOT = '/sdcard/Android/data/com.wither.sms.thor/files'


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--prepared', type=Path, default=ROOT / 'build/sunshine-thor')
    args = parser.parse_args()
    if not args.serial.strip() or any(c.isspace() for c in args.serial):
        parser.error('A single explicit device serial is required')
    folder = args.prepared.resolve(strict=True)
    receipt = json.loads((folder / 'input/verification.json').read_text())
    image = folder / 'GMSE01.iso'
    digest = sha(image)
    if receipt['game_id'] != 'GMSE01' or receipt['revision'] != 0 or digest != receipt['iso_sha256']:
        raise ValueError('Owner disc does not match its verified preparation receipt')
    adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
    package = subprocess.check_output(adb + ['shell', 'pm', 'path', 'com.wither.sms.thor'], text=True)
    if not package.strip().startswith('package:'):
        raise ValueError('Install the separate Sunshine Thor app before staging')
    destination = REMOTE_ROOT + '/disc/game.iso'
    exists = subprocess.run(adb + ['shell', 'test', '-f', destination]).returncode == 0
    if exists:
        prior = subprocess.check_output(adb + ['shell', 'sha256sum', destination], text=True).split()[0]
        if prior != digest:
            raise ValueError('Existing Sunshine image differs; preserve it before replacing it')
        print('Existing Sunshine disc already matches; no transfer needed')
    else:
        subprocess.run(adb + ['shell', 'mkdir', '-p', REMOTE_ROOT + '/disc',
                              REMOTE_ROOT + '/game/cache', REMOTE_ROOT + '/saves/card-a'], check=True)
        temporary = destination + '.part'
        if subprocess.run(adb + ['shell', 'test', '-e', temporary]).returncode == 0:
            raise ValueError('Existing partial transfer retained; inspect it before retrying')
        subprocess.run(adb + ['push', str(image), temporary], check=True, timeout=300)
        actual = subprocess.check_output(adb + ['shell', 'sha256sum', temporary], text=True).split()[0]
        if actual != digest:
            raise ValueError('Transferred image hash differs; partial file retained for diagnosis')
        subprocess.run(adb + ['shell', 'mv', temporary, destination], check=True)
    staged = {
        'status': 'disc_staged_and_device_hash_matched_not_boot_evidence',
        'serial': args.serial, 'package': 'com.wither.sms.thor',
        'remote_disc': destination, 'sha256': digest, 'bytes': image.stat().st_size,
        'other_games_changed': False, 'activity_launched': False,
    }
    (folder / 'disc-staging.json').write_text(json.dumps(staged, indent=2) + '\n')
    print('Sunshine disc staged and hash verified. No Activity was opened.')


if __name__ == '__main__':
    main()
