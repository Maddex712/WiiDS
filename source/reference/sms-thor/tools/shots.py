#!/usr/bin/env python3
"""Convert captured frames (build/shots-raw/field*.ppm, from SMS_SHOTS with
SMS_SHOT_DIR=build/shots-raw) to PNG in build/shots/ and build side-by-side
comparisons with the retail captures of the same field.

usage: tools/shots.py [raw_dir] [out_dir] [retail_dir]
Writes <out>/fieldNNNNN.png and <out>/compare-fieldNNNNN.png (port left,
retail right) and prints a mean-absolute-difference score per field."""
import os, re, sys
from PIL import Image, ImageChops, ImageStat

here = os.path.dirname(os.path.abspath(__file__))
raw = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', 'build', 'shots-raw')
shots = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '..', 'build', 'shots')
retail = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.environ.get('DOLPHIN_ORACLE', ''), 'shots')
if not os.path.isdir(retail):
    sys.exit('retail shots not found at %r: pass the folder or set DOLPHIN_ORACLE' % retail)
os.makedirs(shots, exist_ok=True)
for f in sorted(os.listdir(raw)):
    m = re.match(r'field(\d+)\.ppm$', f)
    if not m:
        continue
    n = int(m.group(1))
    im = Image.open(os.path.join(raw, f)).convert('RGB')
    png = os.path.join(shots, 'field%05d.png' % n)
    im.save(png)
    ref = os.path.join(retail, 'retail-field%05d.png' % n)
    if not os.path.exists(ref):
        print('field %5d: %s (no retail capture)' % (n, png))
        continue
    r = Image.open(ref).convert('RGB')
    a = im.resize(r.size)
    diff = sum(ImageStat.Stat(ImageChops.difference(a, r)).mean) / 3
    cmp_ = Image.new('RGB', (r.width * 2 + 8, r.height), (40, 40, 40))
    cmp_.paste(a, (0, 0))
    cmp_.paste(r, (r.width + 8, 0))
    cmp_.save(os.path.join(shots, 'compare-field%05d.png' % n))
    print('field %5d: mean abs diff vs retail %.1f / 255  -> compare-field%05d.png' % (n, diff, n))
