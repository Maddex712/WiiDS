"""Privately download Eclipse v1.1.0 and patch the owner's verified Sunshine ISO.

Requires requests and py7zr. No game image is downloaded; original ISO is read-only.
All downloaded/game-derived artifacts and durable receipts stay below build/.
The archive URLs/checksums are pinned by upstream sms-pc-port/tools/mods/get.py.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import time
import traceback

import requests
import py7zr

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = 'super_mario_eclipse_v110.7z'
ARCHIVE_SIZE = 892257115
ARCHIVE_MD5 = '37c1805ab88b2a3bb2a96bcdf6884433'
SOURCE_MD5 = '0c6d2edae9fdf40dfc410ff1623e4119'
RESULT_MD5 = 'caa546309e0443f7b47632b040a250d7'
PATCH = 'patches/v1.1.0.xdelta'
PATCH_SIZE = 1191516980
URLS = ['https://sms-eclipse.tekrantgaming.com/files/' + ARCHIVE,
        'https://gamebanana.com/dl/1729332']


def hashes(path):
    md5, sha = hashlib.md5(), hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(4 << 20), b''):
            md5.update(block)
            sha.update(block)
    return {'path': str(path), 'bytes': path.stat().st_size,
            'md5': md5.hexdigest(), 'sha256': sha.hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build/sunshine-thor/GMSE01.iso')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/eclipse-thor/mod')
    parser.add_argument('--patcher', type=Path, default=ROOT / 'reference/sms-android/tools/mods/vcdiff.py')
    args = parser.parse_args()
    folder = args.output.resolve()
    if ROOT / 'build' not in folder.parents:
        parser.error('Private output must stay below this workspace build directory')
    folder.mkdir(parents=True, exist_ok=True)
    receipt_path = folder / 'verification.json'
    receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {
        'version': '1.1.0', 'author': 'Eclipse Team',
        'mod_page': 'https://gamebanana.com/mods/536309',
        'metadata_source': 'reference/sms-android/tools/mods/get.py',
        'urls': URLS, 'events': [], 'device_changed': False,
        'source_opened_read_only': True}

    def checkpoint(status, **fields):
        receipt.update(status=status, updated_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), **fields)
        temp = receipt_path.with_suffix('.json.tmp')
        temp.write_text(json.dumps(receipt, indent=2) + '\n')
        os.replace(temp, receipt_path)

    try:
        source = args.source.resolve(strict=True)
        checkpoint('checking_owner_source')
        source_hash = hashes(source)
        if source_hash['md5'] != SOURCE_MD5:
            raise ValueError('Owner ISO differs from the exact Eclipse patch input; no patch applied')
        checkpoint('owner_source_verified', source=source_hash)
        archive = folder / ARCHIVE
        partial = folder / (ARCHIVE + '.part')
        if not archive.exists():
            for attempt in range(8):
                url = URLS[attempt % len(URLS)]
                start = partial.stat().st_size if partial.exists() else 0
                if start == ARCHIVE_SIZE:
                    break
                if start > ARCHIVE_SIZE:
                    raise ValueError('Retained partial archive exceeds expected length')
                checkpoint('downloading', download_bytes=start, expected_bytes=ARCHIVE_SIZE, download_url=url)
                headers = {'User-Agent': 'sms-pc-port Thor private mod installer', 'Accept-Encoding': 'identity'}
                if start:
                    headers['Range'] = f'bytes={start}-'
                try:
                    with requests.get(url, headers=headers, stream=True, timeout=(30, 60)) as response:
                        response.raise_for_status()
                        if start and (response.status_code != 206 or not response.headers.get('Content-Range', '').startswith(f'bytes {start}-')):
                            raise ValueError('Server did not honor checkpoint resume; prior partial retained')
                        if not start and response.status_code not in (200, 206):
                            raise ValueError('Unexpected download status')
                        receipt['resolved_url'] = response.url
                        receipt['events'].append({'attempt': attempt + 1, 'url': url, 'start': start})
                        last = 0
                        with partial.open('ab' if start else 'wb') as stream:
                            for block in response.iter_content(1 << 20):
                                if not block:
                                    continue
                                stream.write(block)
                                start += len(block)
                                if start > ARCHIVE_SIZE:
                                    raise ValueError('Archive download exceeds pinned size')
                                if time.monotonic() - last > 10:
                                    stream.flush()
                                    checkpoint('downloading', download_bytes=start)
                                    print(f'Download {start}/{ARCHIVE_SIZE} ({start * 100 / ARCHIVE_SIZE:.1f}%)', flush=True)
                                    last = time.monotonic()
                    if start != ARCHIVE_SIZE:
                        raise ValueError('Truncated download; checkpoint retained')
                    break
                except (requests.RequestException, ValueError) as error:
                    receipt['events'].append({'error': str(error), 'url': url})
                    checkpoint('download_interrupted', download_bytes=start)
                    print(f'Download attempt retained: {error}', flush=True)
                    if attempt == 7:
                        raise
                    time.sleep(2)
            downloaded = hashes(partial)
            if downloaded['bytes'] != ARCHIVE_SIZE or downloaded['md5'] != ARCHIVE_MD5:
                raise ValueError('Official archive did not match pinned size/MD5; retained for diagnosis')
            os.replace(partial, archive)
        archive_hash = hashes(archive)
        if archive_hash['bytes'] != ARCHIVE_SIZE or archive_hash['md5'] != ARCHIVE_MD5:
            raise ValueError('Existing archive did not match pinned size/MD5')
        checkpoint('archive_verified', archive=archive_hash)
        patch = folder / PATCH
        if not patch.exists():
            checkpoint('extracting_patch')
            with py7zr.SevenZipFile(archive, mode='r') as packed:
                entries = {entry.filename: entry for entry in packed.list()}
                if PATCH not in entries or entries[PATCH].uncompressed != PATCH_SIZE:
                    raise ValueError('Pinned patch member absent or wrong length')
                packed.extract(path=folder, targets=[PATCH])
        if patch.stat().st_size != PATCH_SIZE:
            raise ValueError('Extracted patch has wrong length; retained for diagnosis')
        checkpoint('patch_extracted', patch=hashes(patch))
        output = folder / 'GMSE04.iso'
        if not output.exists():
            unfinished = folder / 'GMSE04.iso.part'
            if unfinished.exists():
                raise ValueError('Prior incomplete patch output retained; inspect before retrying')
            patcher = args.patcher.resolve(strict=True)
            checkpoint('patching', patcher=hashes(patcher))
            spec = importlib.util.spec_from_file_location('eclipse_vcdiff', patcher)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            progress_at = [0.0]
            def progress(done, total):
                if time.monotonic() - progress_at[0] > 10:
                    checkpoint('patching', patch_bytes_consumed=done, patch_bytes_total=total)
                    print(f'Patch {done * 100 / total:.1f}%', flush=True)
                    progress_at[0] = time.monotonic()
            module.apply(str(source), str(patch), str(unfinished), progress=progress)
            result = hashes(unfinished)
            if result['md5'] != RESULT_MD5:
                raise ValueError('Patched image differs from official GMSE04 MD5; partial retained')
            os.replace(unfinished, output)
        result = hashes(output)
        if result['md5'] != RESULT_MD5:
            raise ValueError('Existing GMSE04 image differs from official MD5')
        with output.open('rb') as stream:
            if stream.read(6) != b'GMSE04':
                raise ValueError('Patched image has unexpected disc ID')
        # Source was never opened for writing; verify its identity after patching as well.
        after = hashes(source)
        if after != source_hash:
            raise ValueError('Owner source identity changed during preparation')
        checkpoint('verified_patched_iso_not_boot_evidence', result=result,
                   original_source_unchanged=True, original_saves_accessed=False)
        print(f'Verified Eclipse ISO: {output}', flush=True)
    except BaseException as error:
        checkpoint('failed_artifacts_retained', error=str(error))
        raise


if __name__ == '__main__':
    main()
