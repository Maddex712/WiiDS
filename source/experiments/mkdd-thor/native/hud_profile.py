"""Build texture fingerprints for the verified USA race HUD, without copying game bytes.

The generated header stays beside the PRIVATE recompilation output, outside this repository.
Run with Python -I. Fingerprints use the pinned gcn-recomp texture cache's hash function.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import struct
import sys

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
MASK = (1 << 64) - 1
PRIME = 0x100000001b3


def texture_hash(data):
    lanes = [0xcbf29ce484222325 ^ x for x in
             (0, 0x9e3779b97f4a7c15, 0x3c6ef372fe94f82b, 0xdaa66d2b78dd3b40)]
    pos = 0
    while pos + 32 <= len(data):
        words = struct.unpack_from('<4Q', data, pos)
        for i, word in enumerate(words):
            h = ((lanes[i] ^ word) * PRIME) & MASK
            lanes[i] = h ^ (h >> 29)
        pos += 32
    h = ((lanes[0] ^ ((lanes[1] * PRIME) & MASK)) ^
         (((lanes[2] ^ ((lanes[3] * PRIME) & MASK)) * PRIME) & MASK))
    while pos + 8 <= len(data):
        word, = struct.unpack_from('<Q', data, pos)
        h = ((h ^ word) * PRIME) & MASK
        h ^= h >> 29
        pos += 8
    for byte in data[pos:]:
        h = ((h ^ byte) * PRIME) & MASK
    return h


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    repo = ROOT.parents[1]
    if output.is_relative_to(repo):
        parser.error('The fingerprint header must stay outside the repository')
    spec = importlib.util.spec_from_file_location('mkdd_stage', ROOT/'design/stage_art.py')
    stage = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(stage)
    with args.iso.open('rb') as disc:
        if args.iso.stat().st_size != stage.DISC_BYTES or hashlib.file_digest(disc, 'sha1').hexdigest() != stage.DISC_SHA1:
            parser.error('Expected the complete, verified GM4E01 USA revision 0 disc')
        files = stage.disc_files(disc)

        def read(path):
            offset, length = files[path]
            disc.seek(offset)
            return disc.read(length)

        race = stage.rarc(stage.yaz0(stage.rarc(read('/MRAM.arc'))['race2d.arc']))
        locale = stage.rarc(read('/MRAM_Locale/English/MRAMLoc.arc'))
        wanted = []

        def add(arc, names, family):
            for name in names:
                wanted.append((name, arc[name+'.bti'], family))

        # These are the actual pictures in p1_race.blo and characterindication.blo.
        add(race, stage.CHARACTERS + ['chara_window1', 'chara_window2'], 'Left')
        add(locale, ['position1_'+str(i) for i in range(1, 9)] + ['im_lap1_1'], 'Left')
        add(race, [x+'_mapicon32' for x in stage.MAP_ICONS] + ['speed_mater1', 'startline1'], 'Right')
        add(locale, ['map_icon_player'+str(i) for i in range(1, 9)] + ['im_time1_3', 'speed_km'], 'Right')
        # Time/lap/speed digits share images. Their original pane positions select the side.
        add(race, ['mariofont_number_'+str(i) for i in range(11)] +
            ['mariofont_number_xline', 'mariofontslash', 'mariofontcolon', 'kazu3d_s', 'kazu3dten'] +
            ['deg'+str(i) for i in range(10)], 'Side')
        add(locale, ['mariofontcolon1', 'mariofontcolon2', 'm_best_lap'], 'Side')
        # Both real item slots stay together next to the rank at the upper left.
        add(race, [x[:-4] for x in race if x.startswith('item_') and x.endswith('.bti')] +
            ['slot2'], 'Item')
        add(locale, ['item_specialmozi2'], 'Item')
        # Slot highlights use these shared solid masks: only the p1 item-slot rectangle moves.
        add(race, ['white', 'white3_1', 'white_repeat', 'alphtex', 'light2'], 'SlotMask')
        for course in stage.COURSES.values():
            path = '/Course/'+course+'.arc'
            arc = stage.rarc(stage.yaz0(read(path)))
            add(arc, [course.lower()+'_map'], 'Right')

    entries = {}
    receipt = []
    blocks = {0: (8, 8, 32), 1: (8, 4, 32), 2: (8, 4, 32), 3: (4, 4, 32),
              4: (4, 4, 32), 5: (4, 4, 32), 6: (4, 4, 64), 14: (8, 8, 32)}
    for name, blob, family in wanted:
        fmt, _, width, height = struct.unpack_from('>BBHH', blob)
        bw, bh, block = blocks[fmt]  # All selected HUD textures are non-indexed.
        offset, = struct.unpack_from('>I', blob, 0x1c)
        size = 0
        for level in range(max(1, blob[0x18])):
            size += ((width+bw-1)//bw)*((height+bh-1)//bh)*block
            if offset+size > len(blob):
                raise ValueError('Truncated HUD texture: '+name)
            fingerprint = texture_hash(blob[offset:offset+size])
            previous = entries.get(fingerprint)
            if previous and previous != family:
                # The same bytes are used for a slot mask and an empty item: use the tighter rule.
                if {previous, family} <= {'Item', 'SlotMask'}:
                    family = 'SlotMask'
                else:
                    raise ValueError(f'Conflicting HUD fingerprint: {name}: {previous}/{family}')
            entries[fingerprint] = family
            receipt.append({'name': name, 'levels': level+1, 'hash': f'{fingerprint:016x}', 'family': family})
            width = max(1, width//2)
            height = max(1, height//2)
    lines = ['// PRIVATE fingerprint metadata only; no disc texels or translated instructions.',
             '// Generated by native/hud_profile.py from the verified GM4E01 disc.',
             'static constexpr HudSignature kHudSignatures[] = {']
    lines += [f'    {{0x{h:016x}ull, HudFamily::{family}}},' for h, family in sorted(entries.items())]
    lines += ['};', '']
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines), encoding='utf-8')
    output.with_suffix('.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(f'{len(wanted)} HUD pictures; {len(entries)} texture fingerprints -> {output}')


if __name__ == '__main__':
    main()
