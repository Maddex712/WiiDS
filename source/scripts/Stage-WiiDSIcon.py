"""Package an approved square logo as legacy and mask-safe Android launcher icons."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'reference/driftdroid/android/app/src/main/res'
DENSITIES = {'mdpi': 1, 'hdpi': 1.5, 'xhdpi': 2, 'xxhdpi': 3, 'xxxhdpi': 4}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logo', type=Path)
    parser.add_argument('--receipt-dir', required=True, type=Path)
    args = parser.parse_args()
    args.receipt_dir.mkdir(parents=True, exist_ok=False)
    source = Image.open(args.logo).convert('RGB')
    if source.width != source.height:
        raise ValueError('Expected square approved artwork')
    outputs = {}
    for density, scale in DENSITIES.items():
        folder = RES / f'mipmap-{density}'
        legacy = source.resize((round(48 * scale),) * 2, Image.Resampling.LANCZOS)
        side = round(108 * scale)
        # Approved artwork already has generous margins. This extra 10% on each
        # side keeps its outer strokes inside Android's circular adaptive mask.
        foreground = Image.new('RGB', (side, side), 'white')
        inner = round(side * .8)
        foreground.paste(source.resize((inner, inner), Image.Resampling.LANCZOS),
                         ((side - inner) // 2,) * 2)
        background = Image.new('RGB', (side, side), 'white')
        for name, image in [('ic_launcher.png', legacy), ('ic_launcher_round.png', legacy),
                            ('ic_launcher_foreground.png', foreground),
                            ('ic_launcher_background.png', background)]:
            dest = folder / name
            backup = args.receipt_dir / density / name
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dest, backup)
            image.save(dest)
            outputs[str(dest.relative_to(ROOT))] = {'before_sha256': sha(backup),
                'sha256': sha(dest), 'size': list(image.size)}
    (args.receipt_dir / 'resources.json').write_text(json.dumps({
        'source': str(args.logo.resolve()), 'source_sha256': sha(args.logo),
        'adaptation': 'Lanczos density scaling; white adaptive safe margins; approved artwork unchanged',
        'outputs': outputs}, indent=2) + '\n')
    print(f'Staged {len(outputs)} icon resources; backups in {args.receipt_dir}')


if __name__ == '__main__':
    main()
