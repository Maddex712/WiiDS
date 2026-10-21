"""Exercise the real E1 unpacker with owner inputs and rejected altered inputs; no device access."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--repo', type=Path, default=ROOT / 'reference/nsmbw-android-e1')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/nsmbw-thor/e1/input-profile-test')
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    tool = args.repo.resolve() / 'projects/nsmbw/tools/unpack_game.py'
    cases = []
    with tempfile.TemporaryDirectory(dir=args.work, prefix='profile-') as temporary:
        temp = Path(temporary)
        source = temp / 'input'
        paths = ['sys/boot.bin', 'sys/main.dol'] + [f'files/rels/{name}.rel.LZ' for name in ('d_profileNP', 'd_basesNP', 'd_enemiesNP', 'd_en_bossNP')]
        for name in paths:
            target = source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.disc / name, target)

        def run(name, expected, pattern=None):
            out = temp / name
            result = subprocess.run([sys.executable, str(tool), str(source), str(out)], capture_output=True, text=True)
            if expected:
                assert result.returncode == 0, result.stderr
                assert (out / 'main.dol').read_bytes() == (args.disc / 'sys/main.dol').read_bytes()
                assert len(list(out.glob('*.rel'))) == 4
            else:
                assert result.returncode != 0
                assert not out.exists(), 'Rejected inputs must produce no prepared output'
                assert pattern in result.stderr, result.stderr
            cases.append({'case': name, 'expected_accept': expected, 'passed': True})

        run('canonical-clean-e1', True)
        dol = source / 'sys/main.dol'
        original = dol.read_bytes()
        changed = bytearray(original); changed[-1] ^= 1; dol.write_bytes(changed)
        run('changed-dol-rejected', False, 'canonical clean E1 input profile')
        dol.write_bytes(original)
        rel = source / paths[2]; compressed = rel.read_bytes()
        changed = bytearray(compressed); changed[-1] ^= 1; rel.write_bytes(changed)
        run('changed-rel-rejected', False, 'canonical clean E1 input profile')
        rel.write_bytes(compressed)
        header = source / 'sys/boot.bin'; original_header = header.read_bytes()
        changed = bytearray(original_header); changed[7] = 2; header.write_bytes(changed)
        run('revision-two-header-rejected', False, 'revision 1')
        header.write_bytes(b'SMNE')
        run('short-header-rejected', False, 'incomplete')
    receipt = {'status': 'owner_input_unpacker_checks_passed_not_gameplay', 'cases': cases,
               'unpacker_sha256': hashlib.sha256(tool.read_bytes()).hexdigest(),
               'limits': ['Tests prepare temporary owner data on the host only; no translation, native boot or gameplay is implied.']}
    path = args.work / 'verification.json'
    path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(f'{len(cases)} actual-unpacker owner-input checks passed; {path}')


if __name__ == '__main__': main()
