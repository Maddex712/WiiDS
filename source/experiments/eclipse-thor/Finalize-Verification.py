"""Collect bounded release evidence; never writes application files or sends input."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
from datetime import datetime, timezone
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/eclipse-thor'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--pid', required=True, type=int)
    a = p.parse_args()
    adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', a.serial]
    def call(*args): return subprocess.check_output(adb + list(args), text=True, encoding='utf-8').strip()
    def sha(path):
        with path.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
    artifacts = {}
    for package, filename in [('com.wither.sms.eclipse.thor', 'Eclipse-Thor.apk'),
                              ('io.github.maddex712.wiids', 'WiiDS-Eclipse.apk')]:
        remote = call('shell', 'pm', 'path', package)
        paths = [line.removeprefix('package:') for line in remote.splitlines() if line.startswith('package:')]
        if len(paths) != 1: raise ValueError('Unexpected installed APK layout')
        actual = call('shell', 'sha256sum', paths[0]).split()[0]
        expected = sha(OUT / filename)
        if actual != expected: raise ValueError(f'Installed APK differs: {package}')
        flags = call('shell', 'dumpsys', 'package', package)
        if re.search(r'\bDEBUGGABLE\b', flags): raise ValueError(f'Debug package left installed: {package}')
        artifacts[package] = {'sha256': actual, 'device_hash_matched': True, 'debuggable': False}
    old = json.loads((OUT / 'before/profiles.json').read_text())
    original_apk = call('shell', 'pm', 'path', 'com.wither.sms.thor').removeprefix('package:')
    original_hash = call('shell', 'sha256sum', original_apk).split()[0]
    if original_hash != old['apks']['com.wither.sms.thor']['sha256']:
        raise ValueError('Original standalone Sunshine APK changed')
    subprocess.run([str(Path(__import__('sys').executable)), str(ROOT / 'experiments/eclipse-thor/Verify-Isolation.py'),
                    '--serial', a.serial, '--compare'], check=True)
    log = call('logcat', '-d', '--pid=' + str(a.pid), '-v', 'threadtime')
    (OUT / 'final-release-logcat.txt').write_text(log + '\n')
    events = call('logcat', '-d', '-b', 'events', '-v', 'brief')
    errors = re.findall(r'^.*(?:FATAL EXCEPTION|Fatal signal|JNI DETECTED ERROR).*$' , log, re.M)
    anrs = [line for line in events.splitlines() if 'am_anr' in line and f',{a.pid},' in line]
    if errors or anrs: raise ValueError('Final release has matched fatal/ANR evidence; inspect scoped log')
    remote = '/sdcard/Android/data/com.wither.sms.eclipse.thor/files'
    settings = call('shell', 'cat', remote + '/game/settings.txt')
    card = call('shell', f'find {remote}/saves/card-a -type f -exec sha256sum {{}} \\;')
    (OUT / 'final-eclipse-save-hashes.txt').write_text(card + '\n')
    if 'GMSE04_super_mario_sunshine.dat' not in card: raise ValueError('Normal Eclipse card file not created')
    after = OUT / 'after'
    after.mkdir(exist_ok=True)
    subprocess.run(adb + ['pull', remote + '/saves', str(after / 'eclipse-saves')], capture_output=True, check=True)
    native = json.loads((OUT / 'launcher-native-comparison.json').read_text())
    if not all(native.values()): raise ValueError('Existing launcher native libraries changed')
    receipt = {
        'status': 'installed_with_bounded_release_validation',
        'updated_utc': datetime.now(timezone.utc).isoformat(), 'serial': a.serial,
        'final_test_pid': a.pid, 'artifacts': artifacts,
        'original_standalone_sunshine_apk_unchanged': True,
        'original_sunshine_profiles_unchanged': json.loads((OUT / 'isolation-comparison.json').read_text()),
        'launcher_native_libraries_unchanged': len(native),
        'eclipse_version': '1.1.0', 'settings': settings,
        'matched_fatal_errors': errors, 'matched_final_pid_anrs': anrs,
        'evidence': sorted(str(p.relative_to(ROOT)) for p in (ROOT / 'build/evidence').glob('eclipse-release-*.png')),
        'validated': ['case artwork/open/launch', 'real tutorial and controller dialogue/pause/exit',
                      'existing lower companion on custom areas', 'Eclipse title and new game card creation',
                      'separate original profiles and APK', 'Android Back return to library'],
        'not_claimed': ['all stages or full game', 'audible audio or haptics', 'long-session stability',
                        'sustained performance benchmark or 120 FPS', 'optional save states'],
    }
    (OUT / 'final-verification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    # Evidence contact sheet: existing screenshots, scaled without changing their contents.
    for stem in ['eclipse-release-opening', 'eclipse-release-case']:
        upper = ROOT / f'build/evidence/{stem}-upper.png'
        lower = ROOT / f'build/evidence/{stem}-lower.png'
        if upper.is_file() and lower.is_file():
            top = Image.open(upper).convert('RGB'); bottom = Image.open(lower).convert('RGB')
            top = top.resize((960, 540), Image.Resampling.LANCZOS)
            width = round(bottom.width * 540 / bottom.height)
            bottom = bottom.resize((width, 540), Image.Resampling.LANCZOS)
            preview = Image.new('RGB', (960, 1092), '#101318')
            preview.paste(top, (0, 0)); preview.paste(bottom, ((960 - width) // 2, 552))
            preview.save(OUT / (stem + '-dual.png'))
    print(OUT / 'final-verification.json')


if __name__ == '__main__': main()
