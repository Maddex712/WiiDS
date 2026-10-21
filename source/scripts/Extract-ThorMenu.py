"""Extract private original PAL Wii menu art; never generates replacement art."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / 'build/thor-menu-work'
OUT = ROOT / 'build/thor-map-data'
spec = importlib.util.spec_from_file_location('thor_hud_extract', ROOT / 'scripts/Extract-ThorHud.py')
hud = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hud)
from menu_layout import controller_animations, pane_nodes, render_layout


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def unpack_sources():
    for name in ('MenuOther', 'MenuSingle', 'Title'):
        hud.unpack(hud.DISC / f'{name}.szs', WORK / name)


def extract():
    OUT.mkdir(parents=True, exist_ok=True)
    used = set()
    palette_path = ROOT / 'reference/driftdroid/generated/data_sections_init_blobs/StaticR.bin'
    raw = palette_path.read_bytes()
    palette = [[list(struct.unpack_from('>4h', raw, 0x80895CC0 - 0x805102E0 + slot*24 + i*8))
                for i in range(3)] for slot in range(6)]
    candidates = [
        ('background', 'MenuOther/bg/blyt/bg.brlyt', ['bg_Loop.brlan'], [-416, -228, 416, 228]),
        ('panel', 'MenuOther/bg/blyt/option_bg.brlyt', ['option_bg_Loop.brlan'], [-416, -228, 416, 228]),
        ('header', 'MenuOther/bg/blyt/obi_top.brlyt', [], None),
        ('footer', 'MenuOther/bg/blyt/obi_bottom.brlyt', [], None),
        ('button', 'MenuSingle/button/blyt/common_w102_option_menu.brlyt', ['common_w102_option_menu_free.brlan', 'common_w102_option_menu_fuchi_check_loop.brlan'], None),
        ('button_focus', 'MenuSingle/button/blyt/common_w102_option_menu.brlyt', ['common_w102_option_menu_select.brlan', 'common_w102_option_menu_fuchi_check_loop.brlan'], None),
    ]
    images = {}
    for role, relative, clips, bounds in candidates:
        path = WORK / relative
        animations = [path.parent.parent / 'anim' / clip for clip in clips]
        colors = None
        animation_groups = None
        if role.startswith('button'):
            bindings_path = path.parent.parent / 'ctrl/OptionSelectButton.brctr'
            used.add(bindings_path)
            bindings = controller_animations(bindings_path)
            animations, animation_groups = [], []
            wanted = {'Select': 'Select' if role == 'button_focus' else 'Free',
                      'Loop': 'Loop', 'SelectIn': 'SelectStop', 'OK': 'OKStop'}
            for group in bindings['groups']:
                for entry in group['animations']:
                    if entry['name'] == wanted.get(group['name']):
                        animations.append(path.parent.parent / 'anim' / entry['file'])
                        animation_groups.append(group['panes'])
            c0, c1, base = palette[1 if role == 'button_focus' else 5]
            colors = {'fuchi_pattern': [c0, c1, [255]*4], 'color_base': [base, base, [255]*4]}
        image, rect, poses = render_layout(path, used.add, animations=animations, frame=0,
                                           bounds=bounds, aspect_scale=(1, 1), material_overrides=colors,
                                           animation_groups=animation_groups)
        name = f'menu_{role}.png'
        image.save(OUT / name)
        images[role] = {'texture': name, 'nativeSize': list(image.size), 'size': list(image.size),
                        'sha256': digest(OUT / name), 'source': relative, 'sourceBounds': rect,
                        'animations': [a.relative_to(WORK).as_posix() for a in animations],
                        'animationGroups': animation_groups,
                        'frame': 0, 'panes': poses, 'nineSliceInsets': None,
                        'nineSliceNote': 'Original picture-pane composition; no authored nine-slice declared'}
    images['button_selected'] = images['button_focus']
    original_hud = json.loads((OUT / 'hud.json').read_text(encoding='utf-8'))
    font = original_hud['fonts']['main']
    settings = OUT / 'thor_settings'
    settings.mkdir(exist_ok=True)
    lines = ['MKW_SETTINGS 1']
    native_files = {}
    for role in ('background', 'panel', 'header', 'footer', 'button', 'button_focus'):
        image = Image.open(OUT / images[role]['texture']).convert('RGBA')
        path = settings / f'{role}.rgba'
        path.write_bytes(image.tobytes())
        lines.append(f'texture {role} {image.width} {image.height} {path.name}')
        native_files[path.name] = {'bytes': path.stat().st_size, 'sha256': digest(path)}
    font_image = Image.open(OUT / font['texture']).convert('RGBA')
    font_path = settings / 'font.rgba'
    font_path.write_bytes(font_image.tobytes())
    native_files[font_path.name] = {'bytes': font_path.stat().st_size, 'sha256': digest(font_path)}
    lines.append(f"font {font_image.width} {font_image.height} {font['lineHeight']} {font['ascent']} font.rgba")
    for character, glyph in font['glyphs'].items():
        values = [ord(character)] + [glyph[k] for k in ('x', 'y', 'w', 'h', 'left', 'advance')]
        lines.append('glyph ' + ' '.join(map(str, values)))
    table = settings / 'settings.tsv'
    table.write_text('\n'.join(lines) + '\n', encoding='ascii')
    native_files[table.name] = {'bytes': table.stat().st_size, 'sha256': digest(table)}
    manifest = {'schema': 1, 'kind': 'private-retail-wii-menu', 'images': images,
        'canvas': {'width': 832, 'height': 456, 'bounds': [-416, -228, 416, 228],
                   'adaptation': 'Original layout frame zero, native pixel dimensions; host placement/stretch is an adaptation'},
        'fonts': {'main': font}, 'fontManifest': 'hud.json',
        'archives': {f'{name}.szs': digest(hud.DISC / f'{name}.szs') for name in ('MenuOther', 'MenuSingle')},
        'runtimePalette': {'source': palette_path.relative_to(ROOT).as_posix(), 'sha256': digest(palette_path),
            'tableAddress': '0x80895CC0', 'stride': 24, 'slots': palette, 'normalSlot': 5, 'selectedSlot': 1,
            'verifiedFunctions': ['PAL PushButton::Update 0x805BDD98', 'ControlManipulator::Update 0x805F0040',
                                  'GetHudColor 0x805F03DC', 'base color 0x805F04D8'],
            'rule': 'Original C0/C1 applied to fuchi_pattern; source base applied to color_base'},
        'sources': {p.relative_to(WORK).as_posix(): digest(p) for p in sorted(used)},
        'files': {record['texture']: {'size': record['size'], 'sha256': record['sha256']} for record in images.values()},
        'native': {'directory': 'thor_settings', 'manifest': 'settings.tsv', 'files': native_files,
                   'pixels': 'Original straight RGBA, row-major top-to-bottom; identical to menu PNG decode/font atlas'},
        'limitations': ['Static source animation frame zero; no dynamic Nintendo loop animation.',
                        'No nine-slice authored in these picture panes; resizing/placement is a host adaptation.',
                        'Existing 174-glyph Wii BRFNT font pixels/metrics preserved without tint or extra outline.']}
    (OUT / 'menu.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f"Extracted {len(manifest['files'])} original menu PNGs, {len(font['glyphs'])} glyphs, native TSV/RGBA")


if __name__ == '__main__':
    unpack_sources()
    extract()
