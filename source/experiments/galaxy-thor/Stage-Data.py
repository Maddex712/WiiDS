"""Stage converted Galaxy data under its own UID, verify every hash, then commit."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tarfile
import time
import re
import shlex

ROOT = Path(__file__).resolve().parents[2]
HOST = ROOT / 'build/galaxy-thor/cooked'
PACKAGE = 'io.github.maddex712.wiids.galaxy'
REMOTE = f'/sdcard/Android/data/{PACKAGE}/files'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
parser.add_argument('--resume-pending', help='Verify and commit an already streamed pending directory')
args = parser.parse_args()
ADB = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
def shell(*parts, **kwargs):
    if parts[:2] == ('sh', '-c'):
        parts = ('sh', '-c', shlex.quote(parts[2]))
    return subprocess.run(ADB + ['shell', 'run-as', PACKAGE, *parts],
                          check=True, capture_output=True, **kwargs)

files = sorted(path for path in HOST.rglob('*') if path.is_file())
assert files and not any(p.is_symlink() for p in HOST.rglob('*'))
manifest = ''.join(hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()
                   + '  ' + path.relative_to(HOST).as_posix() + '\n' for path in files)
manifest_path = ROOT / 'build/galaxy-thor/data-manifest.sha256'
manifest_path.write_bytes(manifest.encode('utf-8'))
pending = args.resume_pending or 'GalaxyData.pending-' + str(int(time.time()))
if not re.fullmatch(r'GalaxyData\.pending-[0-9]+', pending):
    raise ValueError('Invalid pending directory name')
if not args.resume_pending:
    shell('mkdir', REMOTE + '/' + pending)
    command = ADB + ['exec-in', 'run-as', PACKAGE, 'tar', '-xf', '-', '-C', REMOTE + '/' + pending]
    with (ROOT / 'build/galaxy-thor/data-stream.log').open('wb') as log:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log, stderr=log)
        try:
            with tarfile.open(fileobj=process.stdin, mode='w|', format=tarfile.GNU_FORMAT) as archive:
                for index, path in enumerate(files):
                    archive.add(path, arcname=path.relative_to(HOST).as_posix(), recursive=False)
                    if index % 300 == 0:
                        print(f'Streamed {index}/{len(files)} files', flush=True)
                archive.add(manifest_path, arcname='data-manifest.sha256')
            process.stdin.close()
            if process.wait() != 0:
                raise RuntimeError('ADB data stream failed; see data-stream.log')
        finally:
            if process.poll() is None:
                process.terminate()

verified = shell('sh', '-c', f'cd {REMOTE}/{pending} && sha256sum -c data-manifest.sha256')
(ROOT / 'build/galaxy-thor/device-data-hashes.log').write_bytes(verified.stdout + verified.stderr)
if len(verified.stdout.splitlines()) != len(files):
    raise RuntimeError('Unexpected number of device hash results')
metadata = {'disc': 'RMGE01', 'revision': 0,
            'discSha1': 'a36105c9fbfff6041dc5babf4318c6a216ad470a',
            'files': len(files), 'bytes': sum(p.stat().st_size for p in files),
            'manifestSha256': hashlib.sha256(manifest.encode()).hexdigest(),
            'allDeviceHashesMatched': True}
subprocess.run(ADB + ['exec-in', 'run-as', PACKAGE, 'tee', f'{REMOTE}/{pending}/.ready'],
               input=json.dumps(metadata).encode(), check=True)
# A failed first push may have created an empty shell-owned target. Preserve
# any previous directory with a unique backup name; never delete or merge it.
probe = subprocess.run(ADB + ['shell', 'run-as', PACKAGE, 'test', '-e', REMOTE + '/GalaxyData'])
if probe.returncode == 0:
    shell('mv', REMOTE + '/GalaxyData', REMOTE + '/GalaxyData.before-' + str(int(time.time())))
shell('mv', REMOTE + '/' + pending, REMOTE + '/GalaxyData')
(ROOT / 'build/galaxy-thor/device-data-verification.json').write_text(json.dumps(metadata, indent=2))
print(json.dumps(metadata), flush=True)
