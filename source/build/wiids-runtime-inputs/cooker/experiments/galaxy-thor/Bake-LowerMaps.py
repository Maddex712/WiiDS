"""Bake the Galaxy lower screen's flat maps from the owner's extracted game data (private).

For each flat stage (a single plane gravity pointing straight down), the planet's collision floor
(KCL) is drawn from above the way the Sunshine companion draws its maps: surface colours from the
collision's sound codes, gentle height tint, slope shading, a soft outline. The background is
transparent so the map sits over the lower screen's space backdrop. Each map gets a frame file
with the world-to-pixel transform (px = x*k + ox, py = z*k + oz) for Mario's live marker.

Output: build/galaxy-thor/lower-art/maps/<Stage>.png + <Stage>.json (never packaged; staged into
files/ThorHud/maps on the device). Usage: python -I Bake-LowerMaps.py [--files <extracted/files>]
"""
from pathlib import Path
import argparse
import json
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
from lowermap import kcl, raster, smgarc  # noqa: E402

# stage -> the planet whose collision is the walkable world (placed at the origin, unrotated)
MAPS = {
    'PeachCastleGardenGalaxy': 'PeachCastleGardenPlanet',
}
W, H, S = 1120, 760, 2  # the lower screen's map panel, drawn at 2x and filtered down

SOUND = ['null', 'Soil', 'Lawn', 'Stone', 'Marble', 'WoodThick', 'WoodThin', 'Metal', 'Snow', 'Ice', 'Shallow',
         'Sand', 'Beach', 'Carpet', 'Mud', 'Honey']
COLOUR = {'null': (176, 166, 150), 'Soil': (176, 132, 86), 'Lawn': (98, 184, 74), 'Stone': (186, 182, 172),
          'Marble': (228, 224, 216), 'WoodThick': (156, 108, 62), 'WoodThin': (182, 132, 82), 'Metal': (152, 162, 178),
          'Snow': (240, 244, 250), 'Ice': (182, 222, 246), 'Shallow': (98, 180, 240), 'Sand': (234, 212, 152),
          'Beach': (238, 222, 172), 'Carpet': (190, 62, 72), 'Mud': (122, 92, 62), 'Honey': (240, 192, 62)}


def bake(files: Path, stage: str, planet: str, out: Path) -> dict:
    for name in ['Floor_code', 'Wall_code', 'Sound_code', 'Camera_through']:
        smgarc.HNAMES[smgarc.jhash(name)] = name
    arc = smgarc.rarc((files / 'ObjectData' / f'{planet}.arc').read_bytes())
    low = planet.lower()
    _, tris, normals, attr = kcl.parse(arc[f'{low}/{low}.kcl'])
    _, pa = smgarc.bcsv(arc[f'{low}/{low}.pa'])
    sound = np.array([pa[a].get('Sound_code', 0) if a < len(pa) else 0 for a in attr])
    floor = np.array([pa[a].get('Floor_code', 0) if a < len(pa) else 0 for a in attr])

    walk = normals[:, 1] > 0.35          # floors only, like the Sunshine map
    walk &= floor != 1                   # no death planes
    T, N, SC = tris[walk], normals[walk], sound[walk]
    V = T.reshape(-1, 3)
    x0, x1 = np.percentile(V[:, 0], [0.3, 99.7])
    z0, z1 = np.percentile(V[:, 2], [0.3, 99.7])
    k = min(W * 0.92 / (x1 - x0), H * 0.92 / (z1 - z0))
    ox = W / 2 - (x0 + x1) / 2 * k
    oz = H / 2 - (z0 + z1) / 2 * k
    ylo, yhi = np.percentile(V[:, 1], [3, 97])

    P = np.empty((len(T), 3, 3))
    P[:, :, 0] = (T[:, :, 0] * k + ox) * S
    P[:, :, 1] = (T[:, :, 2] * k + oz) * S
    P[:, :, 2] = -T[:, :, 1]            # higher floors win where they overlap
    light = np.array([-0.45, 0.8, -0.4])
    light /= np.linalg.norm(light)
    colours = np.zeros((len(T), 3))
    for i in range(len(T)):
        name = SOUND[SC[i]] if SC[i] < len(SOUND) else 'null'
        base = np.array(COLOUR.get(name, COLOUR['null']), float)
        h = np.clip((T[i, :, 1].mean() - ylo) / max(1.0, yhi - ylo), 0, 1)
        shade = (0.80 + 0.30 * h) * (0.64 + 0.42 * max(0.0, float(N[i] @ light)))
        colours[i] = np.clip(base * shade, 0, 255)
    zbuf, cbuf = raster.rasterize(P, colours, W * S, H * S)
    covered = np.isfinite(zbuf).astype(np.float64)
    rgb = raster.downsample(cbuf * covered[:, :, None], S)
    alpha = raster.downsample(covered[:, :, None], S)[:, :, 0]
    with np.errstate(invalid='ignore', divide='ignore'):
        rgb = np.where(alpha[:, :, None] > 0, rgb / np.maximum(alpha[:, :, None], 1e-6), 0)

    land = Image.fromarray(np.dstack([rgb, alpha * 255]).clip(0, 255).astype(np.uint8), 'RGBA')
    # A dark rim and a soft pale glow around the land, so it reads over the starfield.
    mask = land.getchannel('A')
    rim = mask.filter(ImageFilter.MaxFilter(7)).filter(ImageFilter.GaussianBlur(1.2))
    glow = mask.filter(ImageFilter.MaxFilter(15)).filter(ImageFilter.GaussianBlur(9))
    image = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    image.alpha_composite(Image.merge('RGBA', [Image.new('L', (W, H), 150), Image.new('L', (W, H), 210),
                                               Image.new('L', (W, H), 255), glow.point(lambda v: v * 70 // 255)]))
    image.alpha_composite(Image.merge('RGBA', [Image.new('L', (W, H), 18), Image.new('L', (W, H), 28),
                                               Image.new('L', (W, H), 52), rim]))
    image.alpha_composite(land)
    out.mkdir(parents=True, exist_ok=True)
    image.save(out / f'{stage}.png')
    frame = {'stage': stage, 'planet': planet, 'width': W, 'height': H, 'k': k, 'ox': ox, 'oz': oz,
             'triangles': int(len(T))}
    (out / f'{stage}.json').write_text(json.dumps(frame, indent=2), encoding='utf-8')
    return frame


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--files', type=Path, default=ROOT / 'build/galaxy-thor/extracted/files')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/galaxy-thor/lower-art/maps')
    args = parser.parse_args()
    for stage, planet in MAPS.items():
        frame = bake(args.files, stage, planet, args.out)
        print(f"{stage}: {frame['triangles']} floor triangles, k={frame['k']:.5f} -> {args.out / (stage + '.png')}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
