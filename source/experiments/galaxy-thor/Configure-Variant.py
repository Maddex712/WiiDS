"""Apply the complete reviewed patch to a clean pinned Galaxy checkout."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
source = root / 'reference/galaxy-android'
patch = Path(__file__).parent / 'patches/galaxy-android.patch'
pin = '1abcb975927ecb4189949bd51f81e73a04d97566'
assert subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() == pin
assert patch.is_file(), 'Missing exported source patch'
base = ['git', '-C', str(source), 'apply']
already = subprocess.run(base + ['--reverse', '--check', str(patch)], capture_output=True)
if already.returncode == 0:
    print('Complete Thor adaptation already applied')
else:
    subprocess.run(base + ['--check', str(patch)], check=True)
    subprocess.run(base + [str(patch)], check=True)
    print('Complete Thor adaptation applied to pinned checkout')
