#!/usr/bin/env python3
"""Contact sheet of build/shots/fieldNNNNN.png (tools/shots.py writes them): tools/contact.py OUT.png F1 F2 ... (5 per row)."""
import sys, os
from PIL import Image
here = os.path.dirname(os.path.abspath(__file__))
out, ns = sys.argv[1], [int(x) for x in sys.argv[2:]]
ims = [Image.open(os.path.join(here, '..', 'build', 'shots', 'field%05d.png' % n)).resize((256, 192)) for n in ns]
rows = (len(ims) + 4) // 5
sheet = Image.new('RGB', (1280, 192 * rows))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % 5) * 256, (i // 5) * 192))
sheet.save(out)
