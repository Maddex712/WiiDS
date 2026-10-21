"""Stage locally baked map assets into the private Thor test application."""
import argparse
import hashlib
import json
import math
import pathlib
import struct
import subprocess
import tarfile

root = pathlib.Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', help='Required for device staging; omitted for --check-only')
parser.add_argument('--check-only', action='store_true', help='Validate local assets without contacting a device')
args = parser.parse_args()
if args.serial is not None and (not args.serial or any(c.isspace() for c in args.serial)):
    parser.error('--serial must be nonempty without whitespace')
if not args.check_only and not args.serial:
    parser.error('--serial is required for device staging')
adb = str(root / '.tools/platform-tools/adb.exe')
package = 'com.driftdroid.android.thor'
folder = root / 'build/thor-map-data'
maps = sorted(folder.glob('course_*.png'))
expected_maps = {f'course_{i:02}.png' for i in range(32)}
expected_faces = {f'face_{i:02}.png' for i in range(24)}
if {p.name for p in maps} != expected_maps:
    raise SystemExit('Expected exactly course_00.png through course_31.png')
if {p.name for p in folder.glob('face_*.png')} != expected_faces:
    raise SystemExit('Expected exactly face_00.png through face_23.png')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def png_size(path):
    b = path.read_bytes()
    if len(b) < 45 or b[:16] != b'\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR' or b[-8:] != b'IEND\xaeB`\x82':
        raise SystemExit(f'Invalid PNG: {path.name}; run Bake-ThorMaps.py --audit')
    return list(struct.unpack_from('>II', b, 16))


files = []
legacy = []
for i in range(32):
    path = folder / f'course_{i:02}.png'
    receipt = folder / f'course_{i:02}.json'
    record = json.loads(receipt.read_text())
    frame = record.get('frame', [])
    if (record.get('id') != i or record.get('size') != png_size(path) or len(frame) != 4
            or not all(math.isfinite(x) for x in frame) or frame[0] >= frame[2] or frame[1] >= frame[3]):
        raise SystemExit(f'Invalid course receipt: {receipt.name}')
    source = root / 'reference/driftdroid/DiscData/files' / record['source']
    if source.is_file() and digest(source) != record['source_sha256']:
        raise SystemExit(f'Source changed for {path.name}; rebake before staging')
    if 'png_sha256' in record and digest(path) != record['png_sha256']:
        raise SystemExit(f'PNG hash mismatch: {path.name}')
    if 'png_sha256' not in record or 'pipeline_sha256' not in record:
        legacy.append(i)
    files += [path, receipt]
for name in sorted(expected_faces):
    path = folder / name
    if png_size(path) != [32, 32]:
        raise SystemExit(f'Unexpected character icon size: {name}')
    files.append(path)
faces_receipt = folder / 'faces.json'
if faces_receipt.exists():
    records = json.loads(faces_receipt.read_text())['faces']
    if len(records) != 24 or {r['id'] for r in records} != set(range(24)):
        raise SystemExit('Invalid face receipt IDs')
    for record in records:
        name = f"face_{record['id']:02}.png"
        if record['file'] != name or digest(folder / name) != record['sha256']:
            raise SystemExit(f'Invalid face receipt/hash: {name}')
    files.append(faces_receipt)
items_receipt = folder / 'items.json'
item_pngs = {p.name for p in folder.glob('item_*.png')}
item_count = 0
if items_receipt.exists() or item_pngs:
    expected_items = {f'item_{i:02}.png' for i in range(20)}
    if not items_receipt.exists():
        raise SystemExit('Item PNGs require items.json provenance manifest')
    manifest = json.loads(items_receipt.read_text())
    records = manifest['items']
    if (item_pngs != expected_items or manifest.get('schema') != 1 or len(records) != 20
            or {r['id'] for r in records} != set(range(20))
            or manifest.get('none_icon') != -1
            or manifest.get('triple_mushroom_counts') != {'1': 4, '2': 19, '3': 5}
            or manifest.get('source') != 'Scene/UI/Race.szs'):
        raise SystemExit('Invalid or incomplete optional item asset set; expected item_00.png through item_19.png')
    source = root / 'reference/driftdroid/DiscData/files/Scene/UI/Race.szs'
    if source.is_file() and digest(source) != manifest['source_sha256']:
        raise SystemExit('Item source archive changed; extract again before staging')
    for record in records:
        path = folder / f"item_{record['id']:02}.png"
        size = png_size(path)
        if (record['file'] != path.name or record['size'] != size or min(size) < 1
                or max(size) > 1024 or digest(path) != record['sha256']
                or record.get('raw_kind') != (5 if record['id'] == 19 else record['id'])
                or record.get('count') != (2 if record['id'] == 19 else None)
                or not 0 < record.get('alpha_nonzero', 0) <= size[0] * size[1]
                or not 0 <= record.get('alpha_min', -1) < record.get('alpha_max', -1) <= 255
                or path.read_bytes()[25] != 6):
            raise SystemExit(f'Invalid item receipt/image: {path.name}')
        files.append(path)
    files.append(items_receipt)
    item_count = len(records)
hud_receipt = folder / 'hud.json'
hud_pngs = {p.name for p in folder.glob('hud_*.png')}
hud_count = 0
if hud_receipt.exists() or hud_pngs:
    if not hud_receipt.exists():
        raise SystemExit('HUD PNGs require hud.json provenance/metrics manifest')
    hud = json.loads(hud_receipt.read_text(encoding='utf-8'))
    if (hud.get('schema') != 1 or hud.get('kind') != 'private-retail-wii-hud'
            or set(hud.get('files', {})) != hud_pngs or not hud_pngs
            or len(hud.get('ranks', [])) != 12 or len(hud.get('faces', [])) != 24):
        raise SystemExit('Invalid or incomplete optional retail HUD set')
    for name, record in hud['files'].items():
        if pathlib.Path(name).name != name or not name.startswith('hud_') or not name.endswith('.png'):
            raise SystemExit('Invalid HUD asset filename')
        path = folder / name
        if png_size(path) != record['size'] or digest(path) != record['sha256'] or path.read_bytes()[25] != 6:
            raise SystemExit(f'Invalid HUD PNG/hash: {name}')
        files.append(path)
    font = hud['fonts']['main']
    atlas_size = png_size(folder / font['texture'])
    if font['texture'] not in hud['files'] or font['lineHeight'] <= 0 or font['ascent'] < 0:
        raise SystemExit('Invalid retail HUD font metrics')
    for char, glyph in font['glyphs'].items():
        if (len(char) != 1 or min(glyph['x'], glyph['y'], glyph['w'], glyph['h']) < 0
                or glyph['x'] + glyph['w'] > atlas_size[0] or glyph['y'] + glyph['h'] > atlas_size[1]):
            raise SystemExit('Invalid retail HUD glyph rectangle')
    for archive_name, checksum in hud['archives'].items():
        if pathlib.Path(archive_name).name != archive_name:
            raise SystemExit('Invalid HUD archive name')
        archive_source = root / 'reference/driftdroid/DiscData/files/Scene/UI' / archive_name
        if archive_source.is_file() and digest(archive_source) != checksum:
            raise SystemExit(f'HUD source archive changed: {archive_name}')
    files.append(hud_receipt)
    hud_count = len(hud_pngs)
if legacy:
    print(f'Legacy receipts lack pipeline/output hashes for course IDs: {legacy}')
if args.check_only:
    print(f'Validated {len(maps)} maps, 32 course receipts, 24 faces, {item_count} item icons and {hud_count} HUD PNGs; no device contacted')
    raise SystemExit(0)
archive = root / 'build/thor-maps.tar'
with tarfile.open(archive, 'w', format=tarfile.USTAR_FORMAT) as tar:
    for path in sorted(files):
        tar.add(path, arcname=path.name)
command = [adb, '-s', args.serial]
subprocess.run(command + ['shell', 'run-as', package, 'mkdir', '-p', 'files/WiiCompiled/ThorMap'], check=True)
with archive.open('rb') as stream:
    subprocess.run(command + ['exec-in', 'run-as', package, 'tar', '-xf', '-', '-C',
                             'files/WiiCompiled/ThorMap'], stdin=stream, check=True, timeout=120)
print(f'Staged {len(maps)} course maps, character faces, {item_count} item icons and {hud_count} HUD PNGs ({archive.stat().st_size} archive bytes)')
