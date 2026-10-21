"""Verify/convert the owner's GMSE01 rev0 image; never downloads game files."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
DISC_SHA1 = '8d094f2c5c112aba9660f0478b16c7f2caf63cbf'
DOL_SHA256 = '13934c863d649b1ddca1ca4d7748f49d28a571685cbee5fb1542545c32869955'


def digest(path, algorithm):
    h = hashlib.new(algorithm)
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/sunshine-thor')
    parser.add_argument('--dtk', type=Path, default=ROOT / '.tools/dtk.exe')
    parser.add_argument('--extract', action='store_true')
    args = parser.parse_args()
    source = args.image.resolve(strict=True)
    tool = args.dtk.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    logs = output / 'input'
    logs.mkdir(exist_ok=True)
    verify = subprocess.run([str(tool), 'disc', 'verify', '--md5', str(source)],
                            text=True, capture_output=True, check=True)
    report = verify.stdout + verify.stderr
    (logs / 'disc-verify.log').write_text(report, encoding='utf-8')
    if DISC_SHA1 not in report:
        raise ValueError('This native build requires the verified GMSE01 revision0 image')
    iso = output / 'GMSE01.iso'
    if not iso.exists():
        subprocess.run([str(tool), 'disc', 'convert', str(source), str(iso)], check=True)
    with iso.open('rb') as stream:
        header = stream.read(32)
    if header[:6] != b'GMSE01' or header[7] != 0 or digest(iso, 'sha1') != DISC_SHA1:
        raise ValueError('Existing/output ISO does not match the verified original disc')
    extracted = output / 'extracted'
    if args.extract and not extracted.exists():
        subprocess.run([str(tool), 'disc', 'extract', '-q', str(iso), str(extracted)], check=True)
    dol = extracted / 'sys/main.dol'
    if dol.exists() and digest(dol, 'sha256') != DOL_SHA256:
        raise ValueError('Extracted executable differs from the pinned input')
    receipt = {
        'status': 'verified_owner_input_not_boot_evidence',
        'game_id': 'GMSE01', 'revision': 0,
        'input_path': str(source), 'input_sha256': digest(source, 'sha256'),
        'iso_path': str(iso), 'disc_sha1': DISC_SHA1,
        'iso_sha256': digest(iso, 'sha256'), 'iso_bytes': iso.stat().st_size,
        'dol_sha256': digest(dol, 'sha256') if dol.exists() else None,
        'dtk_version': subprocess.check_output([str(tool), '--version'], text=True).strip(),
    }
    (logs / 'verification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Verified GMSE01 revision0. Private ISO:', iso)


if __name__ == '__main__':
    main()
