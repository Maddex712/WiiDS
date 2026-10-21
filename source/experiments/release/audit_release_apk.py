"""Inventory a release APK and detect ROMs, private files and sampled executable blobs.

All native libraries are searched, including Sunshine and Eclipse when bundled. This is a
bounded byte-window regression check, not proof of provenance or a copyright determination.
The source files are read locally; neither their bytes nor their host paths enter the report.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import random
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
ROM_EXTENSIONS = {'.iso', '.gcm', '.wbfs', '.wbf1', '.rvz', '.wia', '.gcz', '.ciso', '.nkit',
                  '.dol', '.rel', '.elf', '.wad', '.nsp', '.xci'}
PRIVATE_EXTENSIONS = {'.jks', '.keystore', '.p12', '.pfx', '.sav', '.srm', '.log', '.bak'}
GAME_ASSET_EXTENSIONS = {'.szs', '.arc', '.brres', '.tpl', '.bti', '.brstm', '.brsar', '.brfnt'}
MOD_ZIPS = {'assets/mods/MSMBWii.zip': 'be135baea3a6caeb6510574e5bf0ed55a6e3fdfbe51d191195134d865faa30cf'}
MOD_PATCHES = {'25c6b08333802bca17c1e6571c7289a6382a32a85af1aa5bd224acfbac6a8bf0':
               'Eclipse 1.1.0 official VCDIFF patch'}
WINDOW = 64


def stream_sha(stream):
    digest = hashlib.sha256()
    while chunk := stream.read(1024 * 1024):
        digest.update(chunk)
    return digest.hexdigest()


def disc_magic(header):
    return (header[:4] in {b'WBFS', b'RVZ\x01', b'WIA\x01', b'CISO'} or
            header[0x18:0x1C] == b'\x5d\x1c\x9e\xa3' or
            header[0x1C:0x20] == b'\xc2\x33\x9f\x3d')


def windows(data, count=12):
    if len(data) < WINDOW:
        return []
    rng = random.Random(hashlib.sha256(data).digest())
    chosen = set()
    for _ in range(count * 100):
        at = rng.randrange(len(data) - WINDOW + 1)
        sample = data[at:at + WINDOW]
        if len(set(sample)) >= 24:
            chosen.add(sample)
        if len(chosen) >= count:
            break
    return sorted(chosen)


def dol_from_iso(path):
    with path.open('rb') as stream:
        stream.seek(0x420)
        offset = struct.unpack('>I', stream.read(4))[0]
        stream.seek(offset)
        header = stream.read(0x100)
        if len(header) != 0x100:
            raise ValueError('Truncated GameCube DOL header')
        offsets = struct.unpack('>18I', header[:0x48])
        sizes = struct.unpack('>18I', header[0x90:0xD8])
        size = max([0x100] + [at + n for at, n in zip(offsets, sizes) if n])
        if size > 64 * 1024 * 1024:
            raise ValueError('Invalid GameCube DOL size')
        stream.seek(offset)
        data = stream.read(size)
        if len(data) != size:
            raise ValueError('Truncated GameCube DOL')
        return data


def local_sources():
    sources = []
    for label, relative in [
        ('MKW main.dol', 'reference/driftdroid/DiscData/sys/main.dol'),
        ('MKW StaticR.rel', 'reference/driftdroid/DiscData/files/rel/StaticR.rel'),
        ('NSMB main.dol', 'build/nsmbw-thor/e1/extracted/sys/main.dol'),
        ('Sunshine main.dol', 'build/sunshine-thor/extracted/sys/main.dol'),
    ]:
        path = ROOT / relative
        if not path.is_file():
            raise FileNotFoundError('Missing private audit input: ' + label)
        sources.append((label, path.read_bytes()))
    eclipse = ROOT / 'build/eclipse-thor/mod/GMSE04.iso'
    if eclipse.is_file():
        sources.append(('Eclipse patched main.dol', dol_from_iso(eclipse)))
    for label, relative in [
        ('MKW loaded section', 'reference/driftdroid/generated/data_sections_init_blobs'),
        ('NSMB loaded section', 'reference/nsmbw-android-e1/generated/data_sections_init_blobs'),
        ('MSMBWii loaded section', 'reference/nsmbw-android-msmbwii/generated/data_sections_init_blobs'),
    ]:
        folder = ROOT / relative
        for path in sorted(folder.glob('*.bin')):
            sources.append((label + ' ' + path.name, path.read_bytes()))
    return sources


def audit(apk, sources):
    findings, inventory, libraries, mods = [], [], {}, []
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            findings.append('Duplicate ZIP entries')
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            name = entry.filename
            path = PurePosixPath(name)
            inventory.append({'path': name, 'bytes': entry.file_size})
            if path.is_absolute() or '..' in path.parts or '\\' in name:
                findings.append('Unsafe APK path: ' + name)
            lower = name.lower()
            suffix = path.suffix.lower()
            if suffix in ROM_EXTENSIONS | PRIVATE_EXTENSIONS | GAME_ASSET_EXTENSIONS:
                findings.append('Forbidden ROM, extracted game asset or private file: ' + name)
            if any(part in {'saves', 'card-a', 'card-b', 'savegames', '.git'} for part in path.parts):
                findings.append('Private data directory: ' + name)
            if suffix == '.pem' and path.name != 'cacert.pem':
                findings.append('Unrecognized PEM: ' + name)
            if suffix in {'.key', '.keys'} or 'keystore.properties' in lower:
                findings.append('Potential private key: ' + name)
            with archive.open(entry) as stream:
                if disc_magic(stream.read(0x40)):
                    findings.append('Disc image header in APK entry: ' + name)
            if suffix in {'.xdelta', '.vcdiff'}:
                with archive.open(entry) as stream:
                    digest = stream_sha(stream)
                if digest not in MOD_PATCHES:
                    findings.append('Unapproved bundled mod patch: ' + name)
                else:
                    mods.append({'path': name, 'sha256': digest, 'bytes': entry.file_size,
                                 'description': MOD_PATCHES[digest]})
            if suffix == '.so':
                libraries[name] = archive.read(entry)
            if suffix == '.zip':
                data = archive.read(entry)
                expected = MOD_ZIPS.get(name)
                if expected is None or hashlib.sha256(data).hexdigest() != expected:
                    findings.append('Unapproved bundled archive: ' + name)
                else:
                    mods.append({'path': name, 'sha256': expected, 'bytes': len(data),
                                 'description': 'MSMBWii authorized mod archive'})
                    with zipfile.ZipFile(io.BytesIO(data)) as mod:
                        for member in mod.infolist():
                            p = PurePosixPath(member.filename)
                            if p.is_absolute() or '..' in p.parts or p.suffix.lower() in ROM_EXTENSIONS | PRIVATE_EXTENSIONS:
                                findings.append('Forbidden file inside mod: ' + member.filename)
            elif suffix in {'.7z', '.rar'}:
                findings.append('Uninspected bundled archive: ' + name)
    if not libraries:
        findings.append('APK contains no native libraries')
    sample_results = []
    for label, source in sources:
        samples = windows(source)
        hits = []
        for name, library in libraries.items():
            count = sum(sample in library for sample in samples)
            if count:
                hits.append({'library': name, 'windows': count})
        sample_results.append({'source': label, 'samples': len(samples), 'hits': hits})
        if hits:
            findings.append('Sampled source executable bytes embedded: ' + label)
    with Path(apk).open('rb') as stream:
        apk_sha = stream_sha(stream)
    return {'schema': 1, 'apk_sha256': apk_sha,
            'status': 'pass' if not findings else 'failed', 'findings': findings,
            'inventory': inventory,
            'approved_mod_payloads': mods,
            'native_libraries': {name: {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                                 for name, data in libraries.items()},
            'source_window_checks': sample_results,
            'limits': ['Deterministic sampled 64-byte windows; not exhaustive raw-byte absence proof.',
                       'Native translated/reimplemented functions are still bundled.',
                       'This check does not establish redistribution permission.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = audit(args.apk, local_sources())
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': report['status'], 'libraries': len(report['native_libraries']),
                      'sources': len(report['source_window_checks']), 'findings': report['findings']}, indent=2))
    raise SystemExit(0 if report['status'] == 'pass' else 1)


if __name__ == '__main__':
    main()
