"""Repair only files not hash-verified after an interrupted raw ADB stream."""
from pathlib import Path
import argparse
import re
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
parser.add_argument('--pending', required=True)
args = parser.parse_args()
if not re.fullmatch(r'GalaxyData\.pending-[0-9]+', args.pending):
    raise ValueError('Invalid pending directory')
source = ROOT / 'build/galaxy-thor/cooked'
good = {line[:-4] for line in (ROOT / 'build/galaxy-thor/resume-hashes.log').read_text().splitlines()
        if line.endswith(': OK')}
files = [p for p in source.rglob('*') if p.is_file() and p.relative_to(source).as_posix() not in good]
archive = ROOT / 'build/galaxy-thor/data-repair.tgz'
with tarfile.open(archive, 'w:gz', compresslevel=1) as tar:
    for path in files:
        tar.add(path, arcname=path.relative_to(source).as_posix(), recursive=False)
    tar.add(ROOT / 'build/galaxy-thor/data-manifest.sha256', arcname='data-manifest.sha256')
print('Repair archive:', len(files), 'files,', archive.stat().st_size, 'bytes', flush=True)
adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
remote = '/data/local/tmp/wiids-galaxy-data-repair.tgz'
subprocess.run(adb + ['push', str(archive), remote], check=True)
subprocess.run(adb + ['shell', 'run-as', 'io.github.maddex712.wiids.galaxy', 'tar', '-xzf', remote,
    '-C', '/sdcard/Android/data/io.github.maddex712.wiids.galaxy/files/' + args.pending], check=True)
print('Repair extracted; complete hash verification is still required.', flush=True)
