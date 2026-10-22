"""Transfer changed APK records and verify the reconstructed signed APK before install."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/galaxy-thor'
BASE = ROOT / 'build/public-release-beta5/WiiDS-0.9.0-beta.5-arm64.apk'
NEW = OUT / 'WiiDS-0.9.0-beta.6-galaxy-dev.apk'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
args = parser.parse_args()
adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
def remote(*parts):
    return subprocess.check_output(adb + ['shell', *parts], text=True).strip()
def file_hash(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()
expected = json.loads((OUT / 'wiids-package-audit.json').read_text())['sha256']
assert file_hash(NEW) == expected
baseline_hash = file_hash(BASE)
installed = remote('pm', 'path', 'io.github.maddex712.wiids')
assert installed.startswith('package:') and len(installed.splitlines()) == 1
installed = installed.removeprefix('package:')
assert remote('sha256sum', installed).split()[0] == baseline_hash, 'Installed baseline differs; refuse delta'

def payload(f, info):
    f.seek(info.header_offset)
    header = f.read(30)
    assert header[:4] == b'PK\x03\x04'
    name, extra = struct.unpack_from('<HH', header, 26)
    return info.header_offset + 30 + name + extra
def range_hash(f, offset, length):
    f.seek(offset); h = hashlib.sha256()
    while length:
        data = f.read(min(length, 1024 * 1024)); assert data
        h.update(data); length -= len(data)
    return h.digest()
def copy_range(f, out, offset, length):
    f.seek(offset)
    while length:
        data = f.read(min(length, 1024 * 1024)); assert data
        out.write(data); length -= len(data)
segments = []
with BASE.open('rb') as a, NEW.open('rb') as b, zipfile.ZipFile(BASE) as za, zipfile.ZipFile(NEW) as zb:
    position = 0
    old = {i.filename: i for i in za.infolist()}
    for item in sorted(zb.infolist(), key=lambda i: i.header_offset):
        start = payload(b, item)
        if start > position:
            segments.append((0, position, start - position))
        match = old.get(item.filename)
        source = payload(a, match) if match else 0
        same = (match is not None and match.CRC == item.CRC and match.compress_type == item.compress_type
                and match.compress_size == item.compress_size and match.file_size == item.file_size)
        same = same and range_hash(a, source, item.compress_size) == range_hash(b, start, item.compress_size)
        segments.append((1, source, item.compress_size) if same else (0, start, item.compress_size))
        position = start + item.compress_size
    if position < NEW.stat().st_size:
        segments.append((0, position, NEW.stat().st_size - position))
    patch = OUT / 'wiids-apk.delta'
    with patch.open('wb') as stream:
        stream.write(struct.pack('<8sQI', b'WDSDEL1\0', NEW.stat().st_size, len(segments)))
        for kind, offset, length in segments:
            stream.write(struct.pack('<BQQ', kind, offset if kind else 0, length))
            if not kind:
                copy_range(b, stream, offset, length)
print('Signed APK delta:', patch.stat().st_size, 'bytes', flush=True)
compiler = ROOT / '.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang.cmd'
exe = OUT / 'apply-wiids-apk-delta'
subprocess.run([str(compiler), '-O2', str(Path(__file__).parent / 'tests/apply_apk_delta.c'), '-o', str(exe)], check=True)
subprocess.run(adb + ['push', str(exe), '/data/local/tmp/apply-wiids-apk-delta'], check=True)
subprocess.run(adb + ['push', str(patch), '/data/local/tmp/wiids-galaxy-apk.delta'], check=True)
remote('chmod', '700', '/data/local/tmp/apply-wiids-apk-delta')
staging = '/data/local/tmp/wiids-galaxy-new.apk'
print(remote('/data/local/tmp/apply-wiids-apk-delta', installed, '/data/local/tmp/wiids-galaxy-apk.delta', staging), flush=True)
assert remote('sha256sum', staging).split()[0] == expected, 'Reconstructed APK SHA-256 mismatch; no installation'
print('Reconstructed signed APK hash matched.', flush=True)
result = remote('pm', 'install', '-r', staging)
assert result == 'Success', result
installed_after = remote('pm', 'path', 'io.github.maddex712.wiids').removeprefix('package:')
assert remote('sha256sum', installed_after).split()[0] == expected
(OUT / 'wiids-device-install.json').write_text(json.dumps({'sha256': expected,
    'installedHashMatched': True, 'deltaBytes': patch.stat().st_size, 'result': 'PASS'}, indent=2))
print('PASS: WiiDS update installed and device hash matched.', flush=True)
