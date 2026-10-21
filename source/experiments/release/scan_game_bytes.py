"""Check a WiiDS APK ships no game executable bytes.

Samples 64-byte windows (skipping low-variety ones such as padding) from each game's main.dol
and module files, and searches every native library in the APK for them. A clean release finds
0 hits; an APK with embedded DOL/REL data finds nearly all of them.
  python scan_game_bytes.py build/WiiDS.apk
"""
import random
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = {
    'Mario Kart Wii': [ROOT / 'reference/driftdroid/DiscData/sys/main.dol',
                       ROOT / 'reference/driftdroid/DiscData/files/rel/StaticR.rel'],
    'NSMB Wii': [ROOT / 'build/nsmbw-thor/e1/extracted/sys/main.dol'],
}
WINDOW, SAMPLES = 64, 150


def windows(data, rng):
    picked, attempts = [], 0
    while len(picked) < SAMPLES and attempts < SAMPLES * 50:
        attempts += 1
        at = rng.randrange(0, len(data) - WINDOW)
        chunk = data[at:at + WINDOW]
        if len(set(chunk)) >= 24:
            picked.append(chunk)
    return picked


def main():
    apk = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build/WiiDS.apk')
    rng = random.Random(712)
    with zipfile.ZipFile(apk) as z:
        libraries = {n: z.read(n) for n in z.namelist() if n.endswith('.so')}
    total = 0
    for game, files in SOURCES.items():
        for path in files:
            samples = windows(path.read_bytes(), rng)
            hits = sum(1 for s in samples if any(s in lib for lib in libraries.values()))
            total += hits
            print(f'{game} {path.name}: {hits}/{len(samples)} windows found in the APK libraries')
    sys.exit(1 if total else 0)


if __name__ == '__main__':
    main()
