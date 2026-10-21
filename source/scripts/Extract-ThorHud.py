"""Extract the user's PAL Wii HUD font/art and authored row layouts locally.

No system fonts, authored replacement gradients, game downloads or public output.
The pinned offline parser is a local build dependency, never bundled source.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import types

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
DEP = ROOT / 'reference/mkw-3ds'
WORK = ROOT / 'build/thor-hud-work'
OUT = ROOT / 'build/thor-map-data'
DISC = ROOT / 'reference/driftdroid/DiscData/files/Scene/UI'
sys.path[:0] = [str(DEP / 'tools'), str(DEP / 'vendor/gclib')]
spec = importlib.util.spec_from_file_location('thor_map_bake', ROOT / 'scripts/Bake-ThorMaps.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)
# The layout/font readers need only TPL decoding. Avoid importing their unrelated
# disc reader and 3DS packaging dependencies; requesting packaging is an error.
def no_package(*args, **kwargs):
    raise RuntimeError('3DS packaging is not part of the Thor HUD extraction')
sys.modules['cook_race_items'] = types.SimpleNamespace(decode_tpl=decoder.decode_tpl,
                                                     cook_image_sprite=no_package)
from unpack_szs import unpack
from cook_race_hud import sections
from cook_race_flow import font_glyphs, parse_layout
from menu_layout import render_layout, messages


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source(path):
    return {'file': path.relative_to(WORK).as_posix(), 'sha256': digest(path)}


def png(image, filename, **extra):
    target = OUT / filename
    image = image.convert('RGBA')
    image.save(target)
    return dict(texture=filename, size=list(image.size), sha256=digest(target),
                rgba_sha256=hashlib.sha256(image.tobytes()).hexdigest(), **extra)


def font_atlas(path):
    """Repack supported Latin glyphs at original BRFNT cell resolution."""
    data = path.read_bytes()
    mapping = {}
    for tag, _, start in sections(data):
        if tag != b'CMAP':
            continue
        first, last, method = struct.unpack_from('>3H', data, start)
        if method == 0:
            initial = struct.unpack_from('>H', data, start + 12)[0]
            mapping.update((code, initial + code - first) for code in range(first, last + 1))
        elif method == 1:
            mapping.update((code, struct.unpack_from('>H', data, start + 12 + 2 * (code-first))[0])
                           for code in range(first, last + 1))
        elif method == 2:
            count = struct.unpack_from('>H', data, start + 12)[0]
            mapping.update(struct.unpack_from('>HH', data, start + 14 + i*4) for i in range(count))
        else:
            raise ValueError(f'Unsupported CMAP encoding: {method}')
    characters = ''.join(chr(code) for code in sorted(mapping)
                         if (32 <= code <= 126 or 160 <= code <= 255) and mapping[code] != 65535)
    info, glyphs = font_glyphs(path, characters)
    atlas = Image.new('RGBA', (1024, 1024))
    x = y = 1
    records = {}
    for character, glyph in glyphs.items():
        image = glyph['image']
        if x + image.width + 1 > atlas.width:
            x = 1
            y += info['cellH'] + 2
        if y + image.height + 1 > atlas.height:
            raise ValueError('Native glyph atlas exceeds 1024x1024')
        atlas.paste(image, (x, y))
        records[character] = {'x': x, 'y': y, 'w': image.width, 'h': image.height,
                              'left': glyph['left'], 'advance': glyph['advance'],
                              'sourceGlyphIndex': mapping[ord(character)],
                              'rgba_sha256': hashlib.sha256(image.tobytes()).hexdigest()}
        x += image.width + 2
    atlas = atlas.crop((0, 0, 1024, y + info['cellH'] + 1))
    return dict(png(atlas, 'hud_font_main.png'), lineHeight=info['linefeed'], ascent=info['ascent'],
                cellWidth=info['cellW'], cellHeight=info['cellH'], baseline=info['baseline'],
                glyphs=records, source=source(path), pixels='Native straight RGBA, no tint/scaling',
                drawRule='At pen(x,y): draw glyph at x+left,y; advance pen by advance. '
                         'For baseline coordinates subtract ascent from y. Scale all metrics together.')


def timer_slot_palette():
    """Read the PAL slot-zero colors used by the game's native HUD tint code."""
    path = ROOT / 'reference/driftdroid/generated/data_sections_init_blobs/StaticR.bin'
    data = path.read_bytes()
    address, base, stride = 0x80895CC0, 0x805102E0, 24
    # GetHudSlotIdColor maps HUD slot zero to table entry one. The code averages
    # the first two RGB colors, truncates, then lightens the bottom two vertices.
    colors = struct.unpack_from('>8h', data, address + stride - base)
    top = [(colors[i] + colors[i + 4]) // 2 for i in range(3)] + [255]
    bottom = [min(top[i] + delta, 255) for i, delta in enumerate((40, 130, 40))] + [255]
    return {'top': top, 'bottom': bottom, 'vertexColors': [top, top, bottom, bottom],
            'source': path.relative_to(ROOT).as_posix(), 'source_sha256': digest(path),
            'tableAddress': '0x80895CC0', 'tableEntry': 1, 'tableRGBA16': list(colors),
            'verifiedFunctions': {'timerInit': '0x807F7C6C', 'hudSlotColorEnable': '0x807EBF20',
                                  'getHudSlotIdColor': '0x805F0440'},
            'rule': 'Average two source RGB colors with truncation; bottom adds (40,130,40), saturating at255'}


def native_timer_tint(image, palette):
    """Bake source-authored native quad vertex modulation into the original art."""
    result = image.convert('RGBA').copy()
    pixels = result.load()
    for y in range(result.height):
        weight = (y + .5) / result.height
        rgb = [(1-weight)*palette['top'][i] + weight*palette['bottom'][i] for i in range(3)]
        for x in range(result.width):
            value = pixels[x, y]
            pixels[x, y] = tuple(round(value[i]*rgb[i]/255) for i in range(3)) + (value[3],)
    return result


def extract():
    OUT.mkdir(parents=True, exist_ok=True)
    for name in ('Race', 'Race_E', 'Font'):
        unpack(DISC / f'{name}.szs', WORK / name)
    race, english = WORK / 'Race', WORK / 'Race_E'
    result = race / 'result'
    layout_path = result / 'blyt/position_12players.brlyt'
    row_layout = parse_layout(layout_path)
    timer_path = english / 'game_image/blyt/game_image_number.brlyt'
    timer_layout = parse_layout(timer_path)
    map_path = race / 'game_image/blyt/common_w039_map_chara_icon.brlyt'
    map_layout = parse_layout(map_path)
    used = {layout_path, timer_path, map_path}
    manifest = {'schema': 1, 'kind': 'private-retail-wii-hud',
                'archives': {f'{name}.szs': digest(DISC / f'{name}.szs')
                             for name in ('Race', 'Race_E', 'Font')},
                'fonts': {'main': font_atlas(WORK / 'Font/kart_kanji_font.brfnt')},
                'rows': {}, 'timer': {}, 'images': {},
                'layouts': {'row': row_layout, 'timer': timer_layout, 'map': map_layout}}
    native_bounds = [-305, -23, 303, 21]
    background_panes = ['line', 'hight_light', 'select_gold_01', 'select_gold_02',
                        'select_base', 'team_color_c', 'grade']
    for key, animation in (('normal', 'c_off'), ('selected', '1p_color')):
        clips = [result / 'anim' / f'position_12players_{animation}.brlan',
                 result / 'anim/position_12players_icon_wait_position.brlan']
        used.update(clips)
        image, bounds, posed = render_layout(layout_path, used.add, animations=clips, frame=0,
                                             aspect_scale=(1, 1), include=background_panes,
                                             bounds=native_bounds)
        manifest['rows'][key] = dict(png(image, f'hud_row_{key}.png'), nativeSize=list(image.size),
            sourceBounds=bounds, nativeRowSize=[600, 40], nineSliceInsets=None,
            nineSliceNote='No authored nine-slice: source uses picture panes. Insets must not be claimed retail.',
            clips=[source(p) for p in clips], frame=0,
            panes={name: posed[name] for name in background_panes},
            alpha='Preserved original transparent gloss; do not synthesize an opaque plate')
    # Preserve the original timer artwork, including transparent outline pixels.
    for digit in range(10):
        path = english / 'game_image/timg' / f'tt_d_number_3d_{digit:02}.tpl'
        used.add(path)
        manifest['images'][f'digit_{digit}'] = png(decoder.decode_tpl(path), f'hud_digit_{digit}.png', source=source(path))
    for key, stem in [('time', 'tt_time_E'), ('colon', 'tt_d_number_3d_coron_00'),
                      ('dot', 'tt_d_number_3d_coron')]:
        path = english / 'game_image/timg' / f'{stem}.tpl'
        used.add(path)
        manifest['images'][key] = png(decoder.decode_tpl(path), f'hud_{key}.png', source=source(path))
    manifest['timer'] = {'label': 'hud_time.png',
        'digits': [f'hud_digit_{i}.png' for i in range(10)],
        'colon': 'hud_colon.png', 'dot': 'hud_dot.png',
        'sourceLayout': source(timer_path), 'font': None,
        'nativeDigitSize': [32, 32], 'nativeLabelSize': [240, 40],
        'note': 'Race timer uses authored TPL digit art and label, not a substitute BRFNT font. '
                'Source original placement/vertex/material values are in layouts.timer.'}
    palette = timer_slot_palette()
    manifest['timer']['nativeSlotPalette'] = palette
    manifest['timer']['raw'] = {key: manifest['timer'][key] for key in ('label', 'digits', 'colon', 'dot')}
    for key in ['time', 'colon', 'dot'] + [f'digit_{i}' for i in range(10)]:
        image = Image.open(OUT / manifest['images'][key]['texture'])
        manifest['images'][key + '_gold'] = png(native_timer_tint(image, palette), f'hud_{key}_gold.png',
            source=manifest['images'][key]['source'],
            conversion='Original TPL RGBA multiplied by verified PAL HUD slot-zero vertex colors; alpha unchanged')
    manifest['timer'].update(label='hud_time_gold.png',
        digits=[f'hud_digit_{i}_gold.png' for i in range(10)],
        colon='hud_colon_gold.png', dot='hud_dot_gold.png')
    manifest['timer']['note'] += ' Preferred filenames bake the verified native player-zero yellow vertex colors; raw filenames remain under timer.raw.'
    manifest['fonts']['main']['nativeTextPanes'] = {}
    for name, key in [('position', 'rank'), ('player_name', 'name')]:
        pane = next(p for p in row_layout['panes'] if p['name'] == name)
        manifest['fonts']['main']['nativeTextPanes'][key] = {
            'pane': name, 'fontSize': pane['font'],
            'scale': [pane['font'][0] / 42, pane['font'][1] / 51],
            'material': row_layout['materials'][pane['material']], 'vertex': pane['vertex'],
            'alpha': pane['alpha'], 'shadowOffset': None,
            'shadowNote': 'No separate rank/name shadow pane or authored shadow offset in this result layout; do not invent an outline'}
    for key, stem in [('searchlight', 'tt_map_chara_searchlight'),
                      ('map_line', 'tt_race_chara_name_line_gray_map_ver00')]:
        path = race / 'game_image/timg' / f'{stem}.tpl'
        used.add(path)
        manifest['images'][key] = png(decoder.decode_tpl(path), f'hud_{key}.png', source=source(path))
    # The stock scoreboard's 64x64 portraits, in the same retail character ID order.
    portraits = []
    for i, face in enumerate(decoder.FACES):
        stem = 'funky' if face == 'fuky' else face
        path = result / 'timg' / f'tt_{stem}_64x64.tpl'
        used.add(path)
        portraits.append(dict(png(decoder.decode_tpl(path), f'hud_face_{i:02}.png'), id=i, source=source(path)))
    manifest['faces'] = portraits
    bmg = english / 'message/Race.bmg'
    used.add(bmg)
    retail_messages = messages(bmg)
    manifest['ranks'] = [{'rank': i + 1, 'text': retail_messages[0x515 + i],
                           'messageId': 0x515 + i, 'font': 'main'} for i in range(12)]
    font = manifest['fonts']['main']
    for rank in manifest['ranks']:
        if not all(c in font['glyphs'] for c in rank['text']):
            raise ValueError('Unmapped retail rank glyph')
    manifest['sourceFiles'] = [source(p) for p in sorted(used)]
    manifest['files'] = {p.name: {'sha256': digest(p), 'size': list(Image.open(p).size)}
                         for p in sorted(OUT.glob('hud_*.png'))}
    manifest['limitations'] = ['Private retail data only; do not redistribute',
        'Row background is original layout at frame zero; no guessed gray plate or replacement gradients',
        'Repositioning/scaling the original Wii row onto a narrow second panel is a presentation adaptation',
        'Wii map uses its own face/shadow/searchlight panes; no invented 3DS speech-bubble asset is supplied']
    (OUT / 'hud.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f"Extracted {len(manifest['files'])} private HUD PNGs, {len(font['glyphs'])} native glyphs and source layouts")


def audit():
    manifest = json.loads((OUT / 'hud.json').read_text(encoding='utf-8'))
    if manifest.get('schema') != 1 or manifest.get('kind') != 'private-retail-wii-hud':
        raise ValueError('Unexpected HUD manifest')
    for name, record in manifest['files'].items():
        if Path(name).name != name or not name.startswith('hud_') or not name.endswith('.png'):
            raise ValueError('Unsafe HUD filename')
        path = OUT / name
        with Image.open(path) as image:
            image.load()
            if (image.mode != 'RGBA' or list(image.size) != record['size']
                    or digest(path) != record['sha256'] or not image.getchannel('A').getbbox()):
                raise ValueError(f'Invalid HUD image: {name}')
    atlas = Image.open(OUT / manifest['fonts']['main']['texture']).convert('RGBA')
    for character, glyph in manifest['fonts']['main']['glyphs'].items():
        if (len(character) != 1 or min(glyph['x'], glyph['y'], glyph['w'], glyph['h']) < 0
                or glyph['x'] + glyph['w'] > atlas.width or glyph['y'] + glyph['h'] > atlas.height):
            raise ValueError('Invalid native font rectangle')
        rect = (glyph['x'], glyph['y'], glyph['x'] + glyph['w'], glyph['y'] + glyph['h'])
        if hashlib.sha256(atlas.crop(rect).tobytes()).hexdigest() != glyph['rgba_sha256']:
            raise ValueError('Font atlas glyph pixels differ from decoded BRFNT cell')
    for archive, checksum in manifest['archives'].items():
        if digest(DISC / archive) != checksum:
            raise ValueError(f'Changed HUD source archive: {archive}')
    palette = manifest['timer']['nativeSlotPalette']
    if digest(ROOT / palette['source']) != palette['source_sha256']:
        raise ValueError('Changed native PAL HUD palette input')
    print(f"HUD audit passed: {len(manifest['files'])} PNGs; original archive hashes and glyph bounds match")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', action='store_true')
    args = parser.parse_args()
    if not args.audit:
        extract()
    audit()
