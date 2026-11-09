"""Check Downloads for a completed compatible ISO; report partial transfer progress.

No source changes, transfer manipulation, extraction, installation or game launch.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import time

DISC_BYTES = 1459978240
DISC_SHA1 = '34bdef709a1ae9799fdabe8e54cff1c7d3665f2b'
DOL_SHA1 = 'e389e50357d4a5076ab3d38672204658f411123d'


def inspect(path):
    if path.suffix.lower() not in ('.iso', '.gcm', '.crdownload', '.part'):
        return None
    try:
        with path.open('rb') as stream:
            header = stream.read(0x440)
            if len(header) != 0x440 or header[:6] != b'GM4E01':
                return None
            if header[0x1c:0x20] != bytes.fromhex('c2339f3d'):
                return None
            stat = path.stat()
            result = {'path': str(path), 'bytes': stat.st_size, 'modified_ns': stat.st_mtime_ns,
                      'disc_id': 'GM4E01', 'revision': header[7], 'status': 'partial'}
            if path.suffix.lower() not in ('.iso', '.gcm') or stat.st_size != DISC_BYTES:
                return result
            # Final extension + complete length + unchanged stat + both hashes.
            disc = hashlib.sha1()
            stream.seek(0)
            for block in iter(lambda: stream.read(4*1024*1024), b''):
                disc.update(block)
            dol_offset = struct.unpack_from('>I', header, 0x420)[0]
            stream.seek(dol_offset)
            dh = stream.read(0x100)
            if len(dh) != 0x100:
                return result
            offsets = struct.unpack_from('>18I', dh)
            sizes = struct.unpack_from('>18I', dh, 0x90)
            dol_size = max(o+s for o,s in zip(offsets,sizes) if s)
            if dol_size > 16*1024*1024 or dol_offset+dol_size > stat.st_size:
                return result
            stream.seek(dol_offset)
            dol = hashlib.sha1(stream.read(dol_size)).hexdigest()
            after = path.stat()
            unchanged = (stat.st_size,stat.st_mtime_ns)==(after.st_size,after.st_mtime_ns)
            result.update(disc_sha1=disc.hexdigest(),dol_sha1=dol)
            result['status'] = 'verified' if unchanged and disc.hexdigest()==DISC_SHA1 and dol==DOL_SHA1 else 'identity_mismatch'
            return result
    except (OSError, ValueError, struct.error):
        return None


def scan(folder):
    return [r for p in folder.iterdir() if p.is_file() and (r := inspect(p))]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--folder', type=Path, default=Path.home()/'Downloads')
    p.add_argument('--seconds', type=int, default=0)
    p.add_argument('--receipt', type=Path)
    a = p.parse_args()
    end = time.monotonic()+max(a.seconds,0)
    while True:
        entries = scan(a.folder)
        report = {'checked_utc':datetime.now(timezone.utc).isoformat(), 'images':entries,
                  'ready': any(x['status']=='verified' for x in entries)}
        if a.receipt:
            a.receipt.parent.mkdir(parents=True,exist_ok=True)
            a.receipt.write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8')
        print(json.dumps(report),flush=True)
        if report['ready'] or time.monotonic()>=end:
            break
        time.sleep(min(10,max(0,end-time.monotonic())))


if __name__ == '__main__':
    main()
