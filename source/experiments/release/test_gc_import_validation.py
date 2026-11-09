"""Exercise the complete native disc reader on synthetic GC data, never owner ROMs."""
import argparse
import bz2
import hashlib
import json
import struct
import subprocess
import zlib
from pathlib import Path

FULL_SIZE = 1459978240


def u32(data, at, value):
    struct.pack_into('>I', data, at, value)


def disc():
    data = bytearray(0x3410)
    data[:6] = b'GMSE01'
    u32(data, 0x1c, 0xc2339f3d)
    title = b'Synthetic import validation'
    data[0x20:0x20 + len(title)] = title
    # Keep compressed fixtures above the reader's existing minimum container size.
    data[0x1000:0x2000] = b''.join(hashlib.sha256(struct.pack('>I', i)).digest() for i in range(128))
    u32(data, 0x420, 0x3000)
    u32(data, 0x424, 0x3200)
    u32(data, 0x428, 0x40)
    # One DOL text section and one FST file; no copyrighted game content.
    u32(data, 0x3000, 0x100)
    u32(data, 0x3048, 0x80003100)
    u32(data, 0x3090, 4)
    u32(data, 0x30e0, 0x80003100)
    data[0x3100:0x3104] = b'TEST'
    u32(data, 0x3200, 0x01000000)
    u32(data, 0x3208, 2)
    u32(data, 0x3210, 0x3400)
    u32(data, 0x3214, 16)
    data[0x3218:0x3221] = b'test.bin\0'
    data[0x3400:0x3410] = bytes(range(16))
    return data


def wia(data, rvz=True, compressed=False):
    """One real raw-data group, with standard header SHA-1s and BZIP2 optional."""
    encode = bz2.compress if compressed else bytes
    chunk = 0x200000
    group_payload = encode(data)
    raw = encode(struct.pack('>QQII', 0x80, len(data) - 0x80, 0, 1))
    # Reserve room so compressed metadata offsets never depend on their own bytes.
    raw_at, groups_at, payload_at = 0x180, 0x200, 0x440
    group = struct.pack('>II', payload_at // 4,
                        len(group_payload) | (0x80000000 if rvz and compressed else 0))
    if rvz:
        group += struct.pack('>I', 0)
    group = encode(group)
    out = bytearray(payload_at)
    out[:4] = b'RVZ\x01' if rvz else b'WIA\x01'
    u32(out, 4, 0x01000000)
    u32(out, 8, 0x01000000)
    u32(out, 12, 0xdc)
    struct.pack_into('>Q', out, 0x24, len(data))
    u32(out, 0x48, 1)
    u32(out, 0x4c, 2 if compressed else 0)
    u32(out, 0x54, chunk)
    out[0x58:0xd8] = data[:0x80]
    out[0x48 + 0xa0:0x48 + 0xb4] = hashlib.sha1(b'').digest()
    u32(out, 0x48 + 0xb4, 1)
    struct.pack_into('>Q', out, 0x48 + 0xb8, raw_at)
    u32(out, 0x48 + 0xc0, len(raw))
    u32(out, 0x48 + 0xc4, 1)
    struct.pack_into('>Q', out, 0x48 + 0xc8, groups_at)
    u32(out, 0x48 + 0xd0, len(group))
    out[raw_at:raw_at + len(raw)] = raw
    out[groups_at:groups_at + len(group)] = group
    out += group_payload
    struct.pack_into('>Q', out, 0x2c, len(out))
    out[0x10:0x24] = hashlib.sha1(out[0x48:0x48 + 0xdc]).digest()
    out[0x34:0x48] = hashlib.sha1(out[:0x34]).digest()
    return out


def gcz(data, compressed):
    block = 0x8000
    payload = bytes(data) + bytes(block - len(data))
    if compressed:
        payload = zlib.compress(payload)
    header = struct.pack('<IIQQII', 0xb10bc001, 0, len(payload), len(data), block, 1)
    return header + struct.pack('<QI', 0 if compressed else (1 << 63), zlib.adler32(payload)) + payload


def ciso(data):
    header = bytearray(0x8000)
    header[:4] = b'CISO'
    struct.pack_into('<I', header, 4, 0x8000)
    header[8] = 1
    return header + data + bytes(0x8000 - len(data))


def fixtures():
    valid = disc()
    cases = [('valid-trimmed.iso', valid, True), ('valid-padded.iso', valid + bytes(64), True),
             ('valid.gcm', valid, True), ('valid.rvz', wia(valid), True),
             ('valid-bzip2.rvz', wia(valid, compressed=True), True),
             ('valid.wia', wia(valid, rvz=False), True),
             ('valid-bzip2.wia', wia(valid, rvz=False, compressed=True), True),
             ('valid.gcz', gcz(valid, True), True), ('valid-raw.gcz', gcz(valid, False), True),
             ('valid.ciso', ciso(valid), True)]
    sparse = disc()
    u32(sparse, 0x3210, 0x10000)
    cases.append(('valid-sparse-tail.ciso', ciso(sparse), True))
    empty = disc()
    u32(empty, 0x3210, 0xffffffff)
    u32(empty, 0x3214, 0)
    cases.append(('valid-empty-file.iso', empty, True))
    modifications = [
        ('missing-dol', 0x420, 0), ('dol-header-truncated', 0x420, len(valid) - 1),
        ('dol-section-truncated', 0x3090, 0x1000), ('dol-section-overflow', 0x3000, 0xfffffff0),
        ('dol-section-overlaps-header', 0x3000, 4), ('empty-dol', 0x3090, 0),
        ('missing-fst', 0x424, 0), ('fst-header-truncated', 0x424, len(valid) - 1),
        ('fst-size-truncated', 0x428, len(valid)), ('fst-count-truncated', 0x3208, 500),
        ('fst-empty', 0x3208, 0), ('fst-not-directory', 0x3200, 0),
        ('file-data-truncated', 0x3214, 17), ('file-range-overflow', 0x3210, 0xfffffff0),
        ('apploader-truncated', 0x2454, 0x10000),
    ]
    for name, at, value in modifications:
        data = disc()
        u32(data, at, value)
        cases.append((name + '.iso', data, False))
    cases.append(('missing-file-last-byte.iso', valid[:-1], False))
    header = valid[:0x440]
    u32(header, 0x420, 0)
    u32(header, 0x424, 0)
    u32(header, 0x428, 0)
    cases += [('header-only.iso', header, False), ('header-only.rvz', wia(header), False),
              ('truncated-content.rvz', wia(valid[:-1], compressed=True), False)]
    wrong = disc()
    u32(wrong, 0x18, 0x5d1c9ea3)
    cases.append(('wii-refused.iso', wrong, False))
    nkit = disc()
    nkit[0x200:0x204] = b'NKIT'
    cases.append(('nkit-refused.iso', nkit, False))
    return cases


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    output_root = Path('M:/WiiDS-bugfix-preview-2026-10-26/rom-import-regression') / args.out.name
    output_root.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, data, expected in fixtures():
        source, dest = args.out / name, output_root / (name + '.iso')
        assert not dest.exists() and not Path(str(dest) + '.part').exists()
        source.write_bytes(data)
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        result = subprocess.run([str(args.tool), 'gc', str(dest), str(source)], capture_output=True, text=True)
        actual = result.returncode == 0
        passed = actual == expected and (dest.exists() == expected)
        if expected and actual:
            passed &= dest.stat().st_size == FULL_SIZE
            with dest.open('rb') as f:
                f.seek(0x3100)
                passed &= f.read(4) == b'TEST'
            output_hash = None
            if name in ('valid-trimmed.iso', 'valid.rvz', 'valid-bzip2.rvz'):
                with dest.open('rb') as f:
                    output_hash = hashlib.file_digest(f, 'sha256').hexdigest()
        if not expected:
            passed &= not Path(str(dest) + '.part').exists()
        passed &= hashlib.sha256(source.read_bytes()).hexdigest() == before
        rows.append({'name': name, 'expected_success': expected, 'exit': result.returncode,
                     'output': result.stdout.strip(), 'source_sha256': before,
                     'published': dest.exists(), 'partial_exists': Path(str(dest) + '.part').exists(),
                     'passed': bool(passed)})
        if expected and actual and output_hash is not None:
            rows[-1]['full_output_sha256'] = output_hash
        for generated in (dest, Path(str(dest) + '.part')):
            assert generated.resolve().parent == output_root.resolve()
            if generated.exists():
                generated.unlink()
        if name in ('header-only.iso', 'truncated-content.rvz'):
            sentinel = b'previous installed image must survive'
            dest.write_bytes(sentinel)
            result = subprocess.run([str(args.tool), 'gc', str(dest), str(source)], capture_output=True, text=True)
            kept = dest.read_bytes() == sentinel if dest.stat().st_size == len(sentinel) else False
            rows.append({'name': 'preserve-existing-' + name, 'exit': result.returncode,
                         'output': result.stdout.strip(), 'existing_destination_preserved': kept,
                         'partial_exists': Path(str(dest) + '.part').exists(),
                         'passed': result.returncode != 0 and kept and not Path(str(dest) + '.part').exists()})
            for generated in (dest, Path(str(dest) + '.part')):
                assert generated.resolve().parent == output_root.resolve()
                if generated.exists():
                    generated.unlink()
    hashes = {row['full_output_sha256'] for row in rows if 'full_output_sha256' in row}
    assert len(hashes) == 1, 'Valid ISO/RVZ/BZIP2 RVZ conversions differ'
    failures = sum(not row['passed'] for row in rows)
    receipt = {'status': 'EXPECTED_BASELINE_FAILURES' if args.baseline and failures else 'PASS' if not failures else 'FAIL',
               'tool_sha256': hashlib.sha256(args.tool.read_bytes()).hexdigest(),
               'checks': len(rows), 'failures': failures, 'scope': 'Complete exact native reader and conversion on synthetic files; no owner ROMs or Android import',
               'valid_iso_rvz_full_output_sha256': next(iter(hashes)),
               'cases': rows}
    (args.out / 'verification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({k:v for k,v in receipt.items() if k != 'cases'}))
    if not args.baseline and failures:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
