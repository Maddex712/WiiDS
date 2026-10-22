"""Generate private Galaxy lower-screen assets from your own extracted and cooked disc.

Run from the full source ZIP or the smaller Galaxy setup ZIP. Requires Python 3.10+,
Pillow and NumPy. The cooked files can be pulled from a completed device import.
No disc, cooked data, NAND, save or settings are distributed by this script.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extracted-files', type=Path, required=True, help='Original disc partition files/ directory')
    parser.add_argument('--cooked-files', type=Path, required=True, help='Converted GalaxyData/files directory')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/galaxy-thor/lower-art')
    parser.add_argument('--adb', type=Path, help='Optional adb executable; requires --serial')
    parser.add_argument('--serial', help='Device to receive only the generated ThorHud assets')
    args = parser.parse_args()
    if bool(args.adb) != bool(args.serial):
        parser.error('--adb and --serial must be used together')
    for path in (args.extracted_files / 'ObjectData/PlanetMapDataTable.arc',
                 args.cooked_files / 'LayoutData/Font.arc'):
        if not path.is_file():
            parser.error('Missing source input: ' + str(path))
    if args.out.exists() and any(args.out.iterdir()):
        parser.error('Choose an empty --out directory to keep previous generated assets intact')
    args.out.mkdir(parents=True, exist_ok=True)

    def run(script, *parameters):
        subprocess.run([sys.executable, '-I', str(HERE / script), *map(str, parameters)], check=True)

    run('Extract-LowerArt.py', '--data', args.cooked_files / 'LayoutData', '--out', args.out)
    run('Extract-LowerFonts.py', args.cooked_files / 'LayoutData/Font.arc', args.out / 'fonts')
    run('Bake-LowerMaps.py', '--files', args.extracted_files, '--out', args.out / 'maps')
    run('Bake-LowerPlanets.py', '--files', args.extracted_files, '--out', args.out / 'planets')
    planets=list((args.out / 'planets').glob('*.planets'))
    if not (args.out / 'planets/HeavensDoorGalaxy.planets').is_file() or not planets:
        raise SystemExit('Gateway planet missing; inspect planets/audit.json before deploying')
    generated=[p for p in args.out.rglob('*') if p.is_file() and not p.name.endswith('_atlas.png')]
    manifest={p.relative_to(args.out).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in generated}
    (args.out / 'SETUP-MANIFEST.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('Generated',len(planets),'galaxies. Inspect planets/audit.json for skipped pieces/stages.')
    if not args.adb:
        print('Assets ready:',args.out)
        return
    adb=[str(args.adb),'-s',args.serial]
    active=subprocess.run(adb+['shell','pidof','io.github.maddex712.wiids.galaxy:game'],capture_output=True,text=True)
    if active.stdout.strip():
        raise SystemExit('Close Galaxy normally before copying lower-screen assets; no game was stopped.')
    remote='/sdcard/Android/data/io.github.maddex712.wiids.galaxy/files/ThorHud/'
    # File-by-file pushes avoid the folder-transfer race observed during development.
    for path in generated:
        relative=path.relative_to(args.out).as_posix()
        subprocess.run(adb+['shell','mkdir','-p',remote+str(Path(relative).parent).replace('\\','/')],check=True)
        subprocess.run(adb+['push',str(path),remote+relative],check=True,stdout=subprocess.DEVNULL)
        digest=subprocess.check_output(adb+['shell','sha256sum',remote+relative],text=True).split()[0]
        if digest != manifest[relative]:
            raise SystemExit('Transfer hash mismatch: '+relative)
    print('All generated ThorHud files transferred and hash-verified; saves/settings untouched.')


if __name__ == '__main__':
    main()
