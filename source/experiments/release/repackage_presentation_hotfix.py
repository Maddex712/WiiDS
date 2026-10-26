"""Replace only a verified presentation payload and versionCode in an unsigned APK.

The input APK must already be an accepted release. Sign the output with its original
owner key after zipalign. No game binaries or application code are rebuilt here.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile


def version_code(data, old, new):
    result = bytearray(data)
    assert struct.unpack_from('<HHI', data) == (3, 8, len(data)), 'Not Android binary XML'
    at, resources, edits = 8, [], []
    while at < len(data):
        kind, header, length = struct.unpack_from('<HHI', data, at)
        assert length >= header >= 8 and at + length <= len(data)
        if kind == 0x180:
            resources = list(struct.unpack_from('<' + 'I' * ((length-header)//4), data, at+header))
        elif kind == 0x102:
            start, size, count = struct.unpack_from('<HHH', data, at+24)
            assert size == 20
            for i in range(count):
                attr = at+16+start+i*size
                name = struct.unpack_from('<I', data, attr+4)[0]
                if name < len(resources) and resources[name] == 0x0101021b:
                    assert data[attr+15] == 0x10
                    assert struct.unpack_from('<I', data, attr+16)[0] == old
                    assert struct.unpack_from('<I', data, attr+8)[0] == 0xffffffff
                    struct.pack_into('<I', result, attr+16, new)
                    edits.append(attr+16)
        at += length
    assert len(edits) == 1 and new > old, 'Expected one increasing Android versionCode'
    return bytes(result)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base-apk', type=Path, required=True)
    p.add_argument('--presentation', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--old-version-code', type=int, required=True)
    p.add_argument('--new-version-code', type=int, required=True)
    a = p.parse_args()
    assert not a.output.exists(), 'Refusing to overwrite an artifact'
    manifest = json.loads((a.presentation/'manifest.json').read_text())
    replacements = {'assets/presentation/manifest.json': (a.presentation/'manifest.json').read_bytes()}
    for e in manifest['files']:
        data = (a.presentation/'files'/e['target']).read_bytes()
        assert len(data) == e['bytes'] and hashlib.sha256(data).hexdigest() == e['sha256']
        replacements['assets/'+e['asset']] = data
    with zipfile.ZipFile(a.base_apk) as old, zipfile.ZipFile(a.output, 'w') as new:
        replacements['AndroidManifest.xml'] = version_code(old.read('AndroidManifest.xml'), a.old_version_code, a.new_version_code)
        for info in old.infolist():
            name = info.filename
            # V2/V3 signature blocks are outside ZIP entries. Remove any V1 signatures.
            if name.startswith('META-INF/') and (name.endswith(('.RSA', '.DSA', '.EC', '.SF')) or name == 'META-INF/MANIFEST.MF'):
                continue
            if name.startswith('assets/presentation/') and name not in replacements:
                continue
            cloned = copy.copy(info)
            if name in replacements:
                data = replacements.pop(name)
                new.writestr(cloned, data)
            else:
                with old.open(info) as src, new.open(cloned, 'w') as dst:
                    shutil.copyfileobj(src, dst, 1024*1024)
        for name, data in replacements.items():
            new.writestr(name, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    print('Unsigned artwork hotfix written; alignment, owner signature, and full comparison required.')


if __name__ == '__main__':
    main()
