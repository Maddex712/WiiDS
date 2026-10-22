"""Bake the Galaxy lower screen's 3D worlds from the owner's extracted game data (private).

For every galaxy, every placed object the game draws as part of the world is recorded, in every
zone and every layer:
  - the planets of the game's planet table, and any other placed object or map part with
    collision (conveyor worlds, island courses, cages, Bowser's stepping blocks), and
  - scenery: any placed object with a model whose class is a map object, map piece or ride
    (lowermap: the game's own NameObjFactory table, by the folder its class lives in), such as
    flower fields, trees, black holes, launch stars, poles, flags and the Observatory's domes,
    except skies, gravity shells, effects and swarms of tiny pickups (EXCLUDED_*).
Enemies, NPCs, bosses and the player are left out (they need their animations).

Each piece is the object's mesh (stored once per model, in the model's own space) and the
object's placement matrix; the lower screen draws it where the game says it is now (its live
state, port/thor_world.h, matched by zone name and l_id) or, without that, at its placement,
hidden if a switch or cutscene makes it appear. A mesh is the game's own low-detail model when it
has one, else its display model when that is small enough, else (solid pieces only) its collision
mesh coloured by surface. Models keep their own textures, texture coordinates, normals and
vertex colours (lowermap/j3dmesh.py), drawn on the GPU as the game draws them.

Each piece records its zone and layer, and the file carries every mission's layer masks per zone
(the scenario data), so the lower screen shows only what the mission being played has. An audit
of every placed object and what became of it is written beside the output (audit.json).

Output: build/galaxy-thor/lower-art/planets/<Galaxy>.planets (never packaged; staged into
files/ThorHud/planets). Little-endian:
  'GPL4', u32 zones, char[48] name per zone, u32 missions, then per mission: u32 scenarioNo,
  u32 layer mask per zone;
  u32 textures, then per texture: u16 width, u16 height, u8 wrapS, u8 wrapT (0 clamp, 1 repeat,
  2 mirror), u16 0, u32 bytes, PNG (RGBA) padded to 4;
  u32 meshes, then per mesh: char[48] name, f32 cx cy cz radius (model space), u32 vertices,
  u32 indices, u32 batches, then per batch: u32 first index, u32 index count, i32 texture (-1
  none), u32 flags (j3dmesh F_*), opaque batches first; then per vertex (28 bytes): f32 x y z,
  i8 nx ny nz, u8 0, f32 u v, u8 r g b a; then u32 indices (counter-clockwise fronts);
  u32 pieces, then per piece (128 bytes): char[48] name, u32 mesh, f32 placement matrix 3x4
  (row-major, with scale), f32 cx cy cz radius (world, at placement), u8 zone, i8 layer (-1
  common), u8 planet (1: from the planet table), u8 kind (0 ground, 1 scenery), i32 l_id,
  u16 gate (1: SW_APPEAR, 2: a cutscene group: hidden until the game shows it; 4: placed by the
  game at run time, drawn only from the live state), u16 child (0: the object; k: its k-th member).
Usage: python -I Bake-LowerPlanets.py [--files <extracted/files>] [--galaxy NAME ...]
"""
from pathlib import Path
import argparse
import collections
import hashlib
import io
import json
import re
import struct
import sys
from functools import lru_cache

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np  # noqa: E402
from lowermap import j3dmesh, kcl  # noqa: E402
from PIL import Image  # noqa: E402
from lowermap import smgarc  # noqa: E402

LAYERS = {'common'} | {'layer' + c for c in 'abcdefghijklmnop'}
FACTORY = ROOT / 'reference/galaxy-android/decomp/src/Game/NameObj/NameObjFactory.cpp'
CLASSES = ROOT / 'reference/galaxy-android/decomp/include/Game'
MIN_RADIUS = 40             # anything smaller is not worth a draw call on a whole-planet view
MAX_DISPLAY_TRIANGLES = 60000
TEXTURE_MAX = 256
WORLD_FOLDERS = {'MapObj', 'Map', 'Ride'}

@lru_cache(maxsize=512)
def _case_names(folder):
    return {p.name.casefold(): p.name for p in Path(folder).iterdir()}

def asset_path(path):
    """Disc stage/model spellings can differ in case from placement names.

    Windows hides this distinction; the on-device converter must resolve it
    explicitly on Android's case-sensitive filesystem.
    """
    path=Path(path)
    if path.exists(): return path
    parent=asset_path(path.parent) if not path.parent.exists() else path.parent
    if not parent.is_dir(): return path
    name=_case_names(str(parent)).get(path.name.casefold())
    return parent/name if name else path
EXCLUDED_CLASSES = {
    # skies and gravity / atmosphere shells
    'Sky', 'ProjectionMapSky', 'Air', 'PriorDrawAir', 'ProjectionMapAir',
    # invisible or collision-only
    'InvisiblePolygonObj', 'TransparentWall',
    # effects and things that are mostly particles
    'ShootingStar', 'PowerStarHalo', 'AirBubble', 'LavaSteam', 'LavaGeyser', 'Sandstorm', 'GlaringLight',
    'MeteorStrikeLauncher', 'SandUpDownEffectObj', 'AstroEffectObj', 'FishGroup', 'NoteFairy', 'Butterfly',
    # swarms of tiny pickups
    'StarPiece', 'QuestionCoin', 'BlueChip', 'YellowChip', 'CoinFlower',
    # a lens flare, kill volumes, fake reflections
    'BrightSun', 'BlackHoleCube', 'SimpleMirrorObj', 'SimpleMirrorReflectionObj',
    # water jets and critters (animated, not ground)
    'FountainBig', 'WaterPressure', 'SeaGull', 'SurfRay', 'Fluff',
    # pickups
    'ItemBubble', 'Koura', 'KouraShine', 'TreasureSpot',
}
EXCLUDED_NAMES = {'DimensionAir',  # gravity-field shells: drawn as effects in the game, not ground
                  'BlackHoleCube', 'Coin', 'CoinFlower', 'ClipVolumeSphere', 'KoopaVS2PartsClipAreaDisplayA',
                  'SpiderThreadAttachPoint', 'Koura', 'KouraShine', 'SeaGull', 'SurfRay', 'Fluff', 'ItemBubble',
                  'FountainBig', 'WaterPressure', 'MirrorReflectionTwinFallLake', 'MirrorModelTwinFallLake'}
EXCLUDED_SUFFIXES = ('GuidePoint', 'ClipArea', 'ClipVolume')  # helpers, never drawn as scenery
EXCLUDED_PREFIXES = ('BenefitItem', 'MorphItem', 'Kinoko1up', 'KinokoOneUp')  # power-up pickups
# Groups that lay their members out themselves at run time (PlantGroup: around the group, on the
# ground): the members are baked as the group's children and drawn only from the live state.
PLANT_MEMBERS = {'FlowerGroup': 'Flower', 'FlowerBlueGroup': 'FlowerBlue', 'CutBushGroup': 'CutBush'}
VERTEX = np.dtype([('p', '<f4', 3), ('n', 'i1', 4), ('t', '<f4', 2), ('c', 'u1', 4)])
SOUND_RGB = [(170, 160, 150), (168, 128, 84), (96, 178, 72), (176, 172, 164), (226, 222, 214), (150, 104, 60),
             (176, 128, 80), (150, 160, 176), (240, 244, 250), (180, 220, 245), (96, 178, 240), (232, 210, 150),
             (238, 220, 170), (190, 60, 70), (120, 90, 60), (240, 190, 60)]


def mtx_tr(t, r, s=(1, 1, 1)):
    """MR::makeMtxTR with scale: Z * Y * X rotation (degrees), then translation."""
    rx, ry, rz = np.radians(r)
    sx, cx, sy, cy, sz, cz = np.sin(rx), np.cos(rx), np.sin(ry), np.cos(ry), np.sin(rz), np.cos(rz)
    R = np.array([[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
                  [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
                  [-sy, cy * sx, cy * cx]])
    M = np.eye(4)
    M[:3, :3] = R @ np.diag(s)
    M[:3, 3] = t
    return M


def rows(files, kind):
    for path, data in files.items():
        if path.split('/')[-1] == kind and path.split('/')[-2] in LAYERS:
            yield from smgarc.bcsv(data)[1]


def layered_rows(files, kinds):
    """(layer index, or -1 for common, row) for the placement kinds in every layer."""
    for path, data in files.items():
        parts = path.split('/')
        if parts[-1] in kinds and parts[-2] in LAYERS:
            layer = -1 if parts[-2] == 'common' else ord(parts[-2][-1]) - ord('a')
            for r in smgarc.bcsv(data)[1]:
                yield layer, r


def pos(r):
    return (r.get('pos_x', 0), r.get('pos_y', 0), r.get('pos_z', 0))


def rot(r):
    return (r.get('dir_x', 0), r.get('dir_y', 0), r.get('dir_z', 0))


def factory():
    """Object name -> (class or creator function, folder of the class, model archive)."""
    src = FACTORY.read_text(encoding='utf-8', errors='replace')
    folder = {}
    for h in CLASSES.rglob('*.hpp'):
        for m in re.finditer(r'^class\s+([A-Za-z0-9_]+)\s*(?::|\{)', h.read_text(encoding='utf-8', errors='replace'), re.M):
            folder.setdefault(m.group(1), h.parent.name)
    out = {}
    for m in re.finditer(r'\{\s*"([^"]+)",\s*([^,]+?),\s*(?:"([^"]*)"|nullptr|0)\s*,?\s*\}', src):
        name, creator, archive = m.group(1), m.group(2).strip(), m.group(3)
        t = re.search(r'<\s*([A-Za-z0-9_]+)\s*>', creator)
        cls = t.group(1) if t else creator.split('::')[-1]
        f = folder.get(cls)
        if f is None and 'SpinDriver' in cls:
            f = 'Ride'
        out[name] = (cls, f or '?', archive or name)
    return out


class Meshes:
    """One model-space mesh per archive, cached: (mesh index, mesh) or None."""

    def __init__(self, objects: Path):
        self.objects = objects
        self.cache = {}

    def archive(self, name):
        path = asset_path(self.objects / f'{name}.arc')
        return smgarc.rarc(path.read_bytes()) if path.is_file() else None

    def model(self, files, name):
        low = name.lower()
        data = files.get(f'{low}/{low}.bdl') or files.get(f'{low}/{low}.bmd')
        if data is None:
            return None
        m = j3dmesh.load(data)
        if not m['batches']:
            return None
        for bt in m['batches']:
            bt['tex'] = None if bt['tex'] is None else (name, bt['tex'])
        m['textures'] = {(name, k): v for k, v in m['textures'].items()}
        m['tris'] = sum(len(bt['idx']) for bt in m['batches'])
        m['source'] = 'model ' + name
        return m

    def collision(self, files, name):
        low = name.lower()
        data = files.get(f'{low}/{low}.kcl')
        if data is None:
            return None
        for n in ['Sound_code']:
            smgarc.HNAMES[smgarc.jhash(n)] = n
        _, tris, _, attr = kcl.parse(data)
        if len(tris) == 0:
            return None
        pa = smgarc.bcsv(files[f'{low}/{low}.pa'])[1] if f'{low}/{low}.pa' in files else []
        snd = [pa[a].get('Sound_code', 0) if a < len(pa) else 0 for a in attr]
        cols = np.array([SOUND_RGB[s] if s < len(SOUND_RGB) else SOUND_RGB[0] for s in snd], np.uint8)
        P = np.asarray(tris, float).reshape(-1, 3)
        idx = np.arange(len(P)).reshape(-1, 3)
        _, weld = np.unique(P.round(2), axis=0, return_inverse=True)
        nrm = j3dmesh.smooth_normals(P, idx, weld.reshape(-1))
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
        col = np.concatenate([np.repeat(cols, 3, 0), np.full((len(P), 1), 255, np.uint8)], 1)
        batch = dict(material=0, tex=None, flags=j3dmesh.F_LIT, pos=P, nrm=nrm, uv=np.zeros((len(P), 2)), col=col, idx=idx)
        return {'batches': [batch], 'textures': {}, 'tris': len(idx), 'source': 'collision ' + name}

    def mesh(self, archive):
        """The archive's drawable mesh, or (None, why)."""
        if archive in self.cache:
            return self.cache[archive]
        mesh, why = None, 'no archive'
        try:
            main = self.archive(archive)
            if main is not None:
                why = 'no model'
                low = self.archive(archive + 'Low')
                if low is not None:
                    mesh = self.model(low, archive + 'Low')
                if mesh is None:
                    mesh = self.model(main, archive)
                    if mesh is not None and mesh['tris'] > MAX_DISPLAY_TRIANGLES:
                        mesh, why = None, 'model too large'
                if mesh is None:
                    mesh = self.collision(main, archive)
        except Exception as failure:  # noqa: BLE001 - recorded in the audit
            mesh, why = None, f'unreadable ({failure})'
        self.cache[archive] = (mesh, why)
        return self.cache[archive]


def has_collision(meshes, archive):
    files = meshes.archive(archive)
    low = archive.lower()
    return files is not None and f'{low}/{low}.kcl' in files


def bake(files: Path, galaxy: str, meshes: Meshes, planets_known: set, table: dict, out: Path, audit: dict) -> int:
    stages = files / 'StageData'
    main = smgarc.rarc(asset_path(stages / f'{galaxy}.arc').read_bytes())
    zones = [(galaxy, np.eye(4), main)]
    for r in rows(main, 'stageobjinfo'):
        path = asset_path(stages / f"{r['name']}.arc")
        if path.is_file():
            zones.append((r['name'], mtx_tr(pos(r), rot(r)), smgarc.rarc(path.read_bytes())))
    # Every mission's layer mask per zone: a column per zone, named by the zone's hash.
    scenario = smgarc.rarc(asset_path(stages / galaxy / f'{galaxy}Scenario.arc').read_bytes())
    data = next(v for k, v in scenario.items() if k.endswith('scenariodata.bcsv'))
    for zone, _, _ in zones:
        smgarc.HNAMES[smgarc.jhash(zone)] = zone
    smgarc.HNAMES[smgarc.jhash('ScenarioNo')] = 'ScenarioNo'
    missions = [(int(r.get('ScenarioNo', 0)), [int(r.get(zone, 0)) & 0xffffffff for zone, _, _ in zones])
                for r in smgarc.bcsv(data)[1]]
    placed, textures, mesh_index, mesh_list = [], Textures(), {}, []
    log = collections.Counter()
    for zi, (zone, Mz, zfiles) in enumerate(zones):
        for layer, r in layered_rows(zfiles, ('objinfo', 'mappartsinfo')):
            name = r.get('name', '')
            cls, folder, archive = table.get(name, (None, '?', name))
            planet = name in planets_known
            solid = planet or has_collision(meshes, archive)

            def note(decision):
                log[(name, cls, folder, decision)] += 1
            if (name in EXCLUDED_NAMES or cls in EXCLUDED_CLASSES or name.startswith(EXCLUDED_PREFIXES)
                    or name.endswith(EXCLUDED_SUFFIXES)):
                note('excluded: effect, sky or pickup')
                continue
            if name.startswith('Invisible'):
                note('excluded: invisible')
                continue
            if r.get('ShapeModelNo', -1) not in (-1, None):
                note('excluded: model chosen at run time (ShapeModelNo)')
                continue
            if not planet and (folder in ('Enemy', 'NPC', 'Player') or (not solid and folder not in WORLD_FOLDERS)):
                note(f'excluded: {folder} (characters need animation)' if folder in ('Enemy', 'NPC', 'Boss', 'Player')
                     else f'excluded: class folder {folder}')
                continue
            gate = (1 if r.get('SW_APPEAR', -1) not in (-1, None) else 0) | (2 if r.get('DemoGroupId', -1) not in (-1, None) else 0)
            if name in PLANT_MEMBERS:
                member = PLANT_MEMBERS[name]
                mesh, why = meshes.mesh(member)
                if mesh is None:
                    note(f'excluded: member {why}')
                    continue
                if member not in mesh_index:
                    mesh_index[member] = len(mesh_list)
                    mesh_list.append(pack_mesh(member, mesh, textures))
                mi = mesh_index[member]
                count = int(r.get('Obj_arg0', -1))
                count = count if count > 0 else 7
                M = Mz @ mtx_tr(pos(r), rot(r))
                centre, radius = M[:3, 3].copy(), float(mesh_list[mi]['radius'] + 400)
                for child in range(1, count + 1):
                    placed.append((member, mi, M, centre, radius, zi, layer, False, 1, int(r.get('l_id', -1)), gate | 4, child))
                note(f'scenery: {count} members, from the live state')
                continue
            mesh, why = meshes.mesh(archive)
            if mesh is None:
                note(f'excluded: {why}')
                continue
            if not solid and mesh['source'].startswith('collision'):
                note('excluded: no model')
                continue
            if archive not in mesh_index:
                mesh_index[archive] = len(mesh_list)
                mesh_list.append(pack_mesh(archive, mesh, textures))
            mi = mesh_index[archive]
            M = Mz @ mtx_tr(pos(r), rot(r), (r.get('scale_x', 1), r.get('scale_y', 1), r.get('scale_z', 1)))
            local_c, local_r = mesh_list[mi]['centre'], mesh_list[mi]['radius']
            centre = M[:3, :3] @ local_c + M[:3, 3]
            radius = float(local_r * np.abs(np.linalg.svd(M[:3, :3], compute_uv=False)).max())
            if not planet and radius < MIN_RADIUS:
                note('excluded: tiny')
                continue
            kind = 0 if solid else 1
            note('ground' if kind == 0 else 'scenery')
            placed.append((name, mi, M, centre, radius, zi, layer, planet, kind, int(r.get('l_id', -1)), gate, 0))
    audit[galaxy] = [dict(name=n, cls=c, folder=f, decision=d, count=k) for (n, c, f, d), k in sorted(log.items())]
    if not placed:
        return 0
    out.mkdir(parents=True, exist_ok=True)
    with open(out / f'{galaxy}.planets', 'wb') as f:
        f.write(b'GPL4' + struct.pack('<I', len(zones)))
        for zone, _, _ in zones:
            f.write(zone.encode('ascii', 'replace')[:47].ljust(48, b'\0'))
        f.write(struct.pack('<I', len(missions)))
        for number, masks in missions:
            f.write(struct.pack('<I', number) + struct.pack(f'<{len(zones)}I', *masks))
        f.write(struct.pack('<I', len(textures.files)))
        for w, h, ws, wt, png in textures.files:
            f.write(struct.pack('<HHBBHI', w, h, ws, wt, 0, len(png)) + png + b'\0' * (-len(png) % 4))
        f.write(struct.pack('<I', len(mesh_list)))
        for m in mesh_list:
            f.write(m['name'].encode('ascii', 'replace')[:47].ljust(48, b'\0'))
            f.write(struct.pack('<4fIII', *m['centre'], m['radius'], len(m['vertices']), len(m['indices']), len(m['batches'])))
            for first, count, tex, flags in m['batches']:
                f.write(struct.pack('<IIiI', first, count, tex, flags))
            f.write(m['vertices'].tobytes())
            f.write(m['indices'].astype('<u4').tobytes())
        f.write(struct.pack('<I', len(placed)))
        for name, mi, M, centre, radius, zi, layer, planet, kind, link, gate, child in placed:
            f.write(name.encode('ascii', 'replace')[:47].ljust(48, b'\0'))
            f.write(struct.pack('<I', mi) + M[:3, :4].astype('<f4').tobytes())
            f.write(struct.pack('<4fBbBBiHH', *centre, radius, zi, layer, 1 if planet else 0, kind, link, gate, child))
    return len(placed)


class Textures:
    """A galaxy's textures, each stored once (by content), downscaled to TEXTURE_MAX, as PNG."""

    def __init__(self):
        self.files, self.by_key, self.by_hash = [], {}, {}

    def index(self, key, tex):
        if key in self.by_key:
            return self.by_key[key]
        rgba, ws, wt = tex
        digest = hashlib.sha1(rgba.tobytes() + bytes([ws, wt])).digest()
        if digest not in self.by_hash:
            im = Image.fromarray(rgba, 'RGBA')
            if max(im.size) > TEXTURE_MAX:
                k = TEXTURE_MAX / max(im.size)
                im = im.resize((max(1, round(im.size[0] * k)), max(1, round(im.size[1] * k))), Image.LANCZOS)
            buf = io.BytesIO()
            im.save(buf, 'PNG', compress_level=7)
            self.by_hash[digest] = len(self.files)
            self.files.append((im.size[0], im.size[1], min(ws, 2), min(wt, 2), buf.getvalue()))
        self.by_key[key] = self.by_hash[digest]
        return self.by_key[key]


def pack_mesh(name, mesh, textures):
    """A model-space mesh as one vertex/index buffer, opaque batches first, with its bounds."""
    order = sorted(mesh['batches'], key=lambda b: (b['flags'] & j3dmesh.F_BLEND) != 0)
    verts, idx, batches, base, first = [], [], [], 0, 0
    for b in order:
        v = np.zeros(len(b['pos']), VERTEX)
        v['p'] = b['pos']
        n = b['nrm'] / np.maximum(np.linalg.norm(b['nrm'], axis=1, keepdims=True), 1e-9)
        v['n'][:, :3] = np.clip(np.round(n * 127), -127, 127)
        v['t'] = b['uv']
        v['c'] = b['col']
        tex = -1 if b['tex'] is None else textures.index(b['tex'], mesh['textures'][b['tex']])
        verts.append(v)
        idx.append(b['idx'].reshape(-1) + base)
        batches.append((first, b['idx'].size, tex, int(b['flags'])))
        base += len(v)
        first += b['idx'].size
    vertices = np.concatenate(verts)
    p = vertices['p'].astype(float)
    centre = (p.min(0) + p.max(0)) / 2
    return {'name': name, 'vertices': vertices, 'indices': np.concatenate(idx), 'batches': batches, 'centre': centre,
            'radius': float(np.linalg.norm(p - centre, axis=1).max())}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--files', type=Path, default=ROOT / 'build/galaxy-thor/extracted/files')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/galaxy-thor/lower-art/planets')
    parser.add_argument('--galaxy', nargs='*')
    args = parser.parse_args()
    for n in ['PlanetName', 'LowFlag', 'SW_APPEAR', 'DemoGroupId', 'ShapeModelNo', 'l_id', 'Obj_arg0']:
        smgarc.HNAMES[smgarc.jhash(n)] = n
    plan = smgarc.rarc((args.files / 'ObjectData' / 'PlanetMapDataTable.arc').read_bytes())
    known = {r['PlanetName'] for r in smgarc.bcsv(list(plan.values())[0])[1]}
    table = factory()
    skip = {'AstroDome', 'EpilogueDemoStage', 'FileSelect', 'LibraryRoom'}  # no play there
    galaxies = args.galaxy or sorted(p.name for p in (args.files / 'StageData').iterdir()
                                     if p.is_dir() and (p / f'{p.name}Scenario.arc').is_file() and p.name not in skip)
    meshes = Meshes(args.files / 'ObjectData')
    audit = {}
    for galaxy in galaxies:
        try:
            n = bake(args.files, galaxy, meshes, known, table, args.out, audit)
        except Exception as failure:  # one odd stage must not stop the rest
            print(f'{galaxy}: skipped ({failure})')
            audit[galaxy] = [dict(name='*', decision=f'stage skipped ({failure})')]
            continue
        if n:
            size = (args.out / f'{galaxy}.planets').stat().st_size
            print(f'{galaxy}: {n} pieces, {size // 1024} KB')
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'audit.json').write_text(json.dumps(audit, indent=1), encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
