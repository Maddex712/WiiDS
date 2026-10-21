"""Stage the authorized, pinned private mod pack into the debug Thor app.

Does not download or include a base-game dump. Verifies every staged file before
renaming the new directory into place; never overwrites an existing installation.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent.parent
PACKAGE = 'com.driftdroid.android.thor'
EXPECTED = '7471e1d2f61c11ab0de6544728406ee1359a4eab736b2dc6a73b741f69e7b4a3'
TARGET = 'files/WiiCompiled/RetroRewind6'
PARTIAL = TARGET + '.partial'


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    args = parser.parse_args()
    if args.serial is not None and (not args.serial or any(c.isspace() for c in args.serial)):
        parser.error('--serial must be nonempty without whitespace')
    pack = args.pack.resolve(strict=True)
    if sha(pack / 'Binaries/Code.pul') != EXPECTED:
        raise SystemExit('Unsupported Code.pul')
    files = sorted(path for path in pack.rglob('*') if path.is_file())
    records = []
    for path in files:
        if path.is_symlink() or not path.resolve().is_relative_to(pack):
            raise SystemExit('Unsafe pack path')
        relative = path.relative_to(pack).as_posix()
        if '\n' in relative or '\r' in relative or '\\' in relative:
            raise SystemExit('Unsafe checksum filename')
        records.append({'path': relative, 'bytes': path.stat().st_size, 'sha256': sha(path)})
    adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
    def shell(*command, **kwargs):
        return subprocess.run(adb + ['shell', 'run-as', PACKAGE, *command], check=True, **kwargs)
    # Existing complete/partial installs require explicit investigation rather than deletion.
    for destination in (TARGET, PARTIAL):
        present = subprocess.run(adb + ['shell', 'run-as', PACKAGE, 'test', '-e', destination])
        if present.returncode == 0:
            raise SystemExit(f'Refusing to overwrite {destination}')
        if present.returncode != 1:
            raise SystemExit('Could not inspect private app storage')
    archive = ROOT / 'build/retro-rewind-device.tar'
    checksums = ''.join(f"{record['sha256']}  {record['path']}\n" for record in records).encode()
    with tarfile.open(archive, 'w', format=tarfile.USTAR_FORMAT) as output:
        for path, record in zip(files, records):
            output.add(path, arcname=record['path'], recursive=False)
        info = tarfile.TarInfo('.thor-pack.sha256')
        info.size = len(checksums)
        output.addfile(info, io.BytesIO(checksums))
    shell('mkdir', '-p', PARTIAL)
    print(f'Staging {len(records)} files, {sum(r["bytes"] for r in records)} bytes', flush=True)
    with archive.open('rb') as stream:
        subprocess.run(adb + ['exec-in', 'run-as', PACKAGE, 'tar', '-xf', '-', '-C', PARTIAL],
                       stdin=stream, check=True, timeout=1800)
    # Both shell arguments are constant app-relative paths, with no input interpolation.
    shell('sh', '-c', f"'cd {PARTIAL} && sha256sum -cs .thor-pack.sha256'", timeout=600)
    shell('mv', PARTIAL, TARGET)
    receipt = {'schema': 1, 'verifiedUtc': datetime.now(timezone.utc).isoformat(),
               'serial': args.serial, 'packRoot': str(pack), 'deviceRoot': TARGET,
               'codePulSha256': EXPECTED, 'files': records,
               'allDeviceHashesVerified': True, 'gameplayVerified': False}
    (ROOT / 'build/retro-rewind-device-staging.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print('Every device file verified; mod directory installed. Gameplay remains untested.')
