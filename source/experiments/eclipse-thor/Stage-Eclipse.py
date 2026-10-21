"""Stage a verified Eclipse disc and the existing Sunshine companion art, separately."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = 'com.wither.sms.eclipse.thor'
REMOTE = '/sdcard/Android/data/' + PACKAGE + '/files'


def sha(path):
    with path.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    args = p.parse_args()
    adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
    def call(*items): return subprocess.check_output(adb + list(items), text=True, encoding='utf-8').strip()
    image = ROOT / 'build/eclipse-thor/mod/GMSE04.iso'
    with image.open('rb') as f: digest = hashlib.file_digest(f, 'md5').hexdigest()
    if digest != 'caa546309e0443f7b47632b040a250d7': raise ValueError('Unverified Eclipse disc')
    with image.open('rb') as f: header = f.read(32)
    if header[:6] != b'GMSE04' or header[28:32] != bytes.fromhex('c2339f3d'): raise ValueError('Unexpected Eclipse header')
    package = call('shell', 'pm', 'path', PACKAGE)
    if not package.startswith('package:'): raise ValueError('Install the separate Eclipse app first')
    expected = sha(image)
    subprocess.run(adb + ['shell', 'mkdir', '-p', REMOTE + '/disc', REMOTE + '/saves/card-a', REMOTE + '/game/cache', REMOTE + '/ThorHud'], check=True)
    dest = REMOTE + '/disc/game.iso'
    if subprocess.run(adb + ['shell', 'test', '-e', dest], capture_output=True).returncode == 0:
        if call('shell', 'sha256sum', dest).split()[0] != expected: raise ValueError('Existing Eclipse disc differs; retained')
    else:
        temp = dest + '.part'
        if subprocess.run(adb + ['shell', 'test', '-e', temp], capture_output=True).returncode == 0:
            if call('shell', 'sha256sum', temp).split()[0] != expected: raise ValueError('Partial Eclipse transfer retained for inspection')
        else:
            subprocess.run(adb + ['push', str(image), temp], check=True, timeout=900)
            if call('shell', 'sha256sum', temp).split()[0] != expected: raise ValueError('Transferred Eclipse hash mismatch')
        subprocess.run(adb + ['shell', 'mv', temp, dest], check=True)
    hud = ROOT / 'build/sunshine-thor/thor-hud'
    assets = {f.name: sha(f) for f in hud.iterdir() if f.is_file()}
    subprocess.run(adb + ['push', str(hud) + '/.', REMOTE + '/ThorHud'], check=True, timeout=180)
    actual = call('shell', f'find {REMOTE}/ThorHud -maxdepth 1 -type f -exec sha256sum {{}} \\;')
    device_assets = {line.split(None, 1)[1].rsplit('/', 1)[-1]: line.split()[0] for line in actual.splitlines()}
    if any(device_assets.get(name) != digest for name, digest in assets.items()): raise ValueError('Companion artwork mismatch')
    (ROOT / 'build/eclipse-thor/device-staging.json').write_text(json.dumps({
        'package': PACKAGE, 'serial': args.serial, 'disc_md5': digest,
        'disc_sha256': expected, 'disc_bytes': image.stat().st_size,
        'remote_disc': dest, 'companion_files': assets, 'device_hashes_matched': True,
        'regular_sunshine_changed': False, 'activity_launched': False,
    }, indent=2) + '\n')
    print('Eclipse disc and companion art device hashes match. No game launched.')


if __name__ == '__main__': main()
