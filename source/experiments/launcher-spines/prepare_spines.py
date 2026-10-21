"""Crop five existing printed spines; adapt approved MSMBWii cover lettering.

Retail/community scan crops are saved at original resolution with no resampling.
Only the clearly identified MSMBWii adaptation and readable contact sheet resample.
"""
from pathlib import Path
import hashlib
import json
from PIL import Image, ImageDraw
import argparse
import io
import os
import urllib.request
from urllib.parse import urlparse

WORKSPACE = Path(__file__).resolve().parents[2]
ROOT = WORKSPACE / "build/case-spines"
OUT = ROOT / "spines"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--download", action="store_true", help="Fetch missing pinned public cover PNGs; never fetch games")
args = parser.parse_args()
pins = json.loads(Path(__file__).with_name("scans.json").read_text(encoding="utf8"))
SOURCE = {item["id"]: [item] for item in pins}
MAX_PNG = 5 * 1024 * 1024

def validate_scan(data, pin):
    if len(data) > MAX_PNG or len(data) != pin["bytes"]:
        raise ValueError(f"Unexpected PNG size: {pin['id']}")
    if not data.startswith(b"\x89PNG\r\n\x1a\n") or hashlib.sha256(data).hexdigest() != pin["sha256"]:
        raise ValueError(f"PNG signature/hash mismatch: {pin['id']}")
    with Image.open(io.BytesIO(data)) as image:
        if image.format != "PNG" or list(image.size) != pin["dimensions"]:
            raise ValueError(f"PNG dimensions mismatch: {pin['id']}")
        image.verify()

for pin in pins:
    url = urlparse(pin["url"])
    if url.scheme != "https" or url.hostname != "art.gametdb.com" or url.query or url.fragment:
        raise ValueError("Only the pinned public GameTDB HTTPS image URL is allowed")
    if Path(pin["filename"]).name != pin["filename"]:
        raise ValueError("Scan filename must be a simple basename")
    path = ROOT / "scans" / pin["filename"]
    if not path.exists():
        if not args.download:
            raise FileNotFoundError(f"Missing scan {pin['filename']}; use --download to fetch the pinned public cover")
        with urllib.request.urlopen(pin["url"], timeout=45) as response:
            final = urlparse(response.geturl())
            if final.scheme != "https" or final.hostname != "art.gametdb.com":
                raise ValueError("Unexpected image redirect")
            data = response.read(MAX_PNG + 1)
        validate_scan(data, pin)
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_name(path.name + f".tmp-{os.getpid()}")
        try:
            with temp.open("xb") as file:
                file.write(data)
            os.replace(temp, path)
        finally:
            temp.unlink(missing_ok=True)
    validate_scan(path.read_bytes(), pin)
OUT.mkdir(parents=True, exist_ok=True)
SPECS = {
    "original": ("RMCE01", (484, 0, 536, 680)),
    "nsmbw": ("SMNE01", (484, 0, 536, 680)),
    "sunshine": ("GMSE01", (489, 0, 540, 680)),
    # This existing community design places the logo slightly over the nominal
    # fold. Preserve that printed artwork; x478 avoids neighboring back text.
    "retro-rewind": ("RMCETO", (478, 0, 536, 680)),
    "eclipse": ("GMSE04", (485, 0, 539, 680)),
}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

records = []
for stem, (gid, rect) in SPECS.items():
    source = ROOT / "scans" / f"{gid}-coverfullHQ-US.png"
    original = Image.open(source)
    crop = original.crop(rect)
    dest = OUT / f"{stem}-spine.png"
    crop.save(dest)
    check = Image.open(dest)
    assert check.tobytes() == original.crop(rect).tobytes()
    remote = SOURCE[gid][0]
    records.append({
        "stem": stem, "file": str(dest.relative_to(ROOT)),
        "dimensions": list(crop.size), "sha256": sha(dest),
        "source_file": str(source.relative_to(ROOT)),
        "source_sha256": sha(source), "source_url": remote["url"],
        "source_page": remote["source_page"],
        "artwork_kind": remote["artwork_kind"],
        "crop_ltrb_exclusive": list(rect),
        "transform": "Exact original-resolution crop; no resizing, rotation, recoloring or synthetic lettering.",
        "orientation": "Portrait; physical top at image top, source lettering reads downwards.",
    })

# No existing MSMBWii full-cover image was found. Its custom spine retains the
# scanned Wii logo/footer and red printed sleeve, and uses only approved existing
# cover pixels for the game's title/subtitle. It is not a retail scan.
base = Image.open(OUT / "nsmbw-spine.png").convert("RGB")
red = base.crop((0, 520, base.width, 680))
for y in range(150, 510, red.height):
    patch = red.crop((0, 0, red.width, min(red.height, 510-y)))
    base.paste(patch, (0, y))
cover = ROOT.parent / "thor-map-data/launcher/msmbwii-cover.png"
logo_rect = (350, 1750, 1220, 1995)
logo = Image.open(cover).convert("RGB").crop(logo_rect)
target_width = base.width - 4
target_length = round(target_width * logo.width / logo.height)
logo = logo.resize((target_length, target_width), Image.Resampling.LANCZOS)
logo = logo.transpose(Image.Transpose.ROTATE_270)
position = ((base.width-logo.width)//2, (base.height-logo.height)//2)
base.paste(logo, position)
dest = OUT / "msmbwii-spine.png"
base.save(dest)
records.append({
    "stem": "msmbwii", "file": str(dest.relative_to(ROOT)),
    "dimensions": list(base.size), "sha256": sha(dest),
    "artwork_kind": "Custom adaptation, not a retail or discovered community spine scan",
    "sources": [
        {"file": "spines/nsmbw-spine.png", "sha256": sha(OUT / "nsmbw-spine.png"),
         "source_url": SOURCE["SMNE01"][0]["url"], "role": "Real retail Wii logo, footer and sampled red sleeve background"},
        {"file": "build/thor-map-data/launcher/msmbwii-cover.png", "sha256": sha(cover), "crop_ltrb_exclusive": list(logo_rect),
         "role": "Existing owner-approved MSMBWii title/subtitle artwork"},
    ],
    "transform": {"old_logo_replaced_y": [150, 510], "background_sample_ltrb": [0,520,52,680],
                  "logo_resize_before_clockwise_rotation": [target_length, target_width],
                  "logo_paste_xy": list(position), "new_text_drawn": False},
    "orientation": "Portrait; physical top at image top, source lettering reads downwards.",
})
(OUT / "spine-provenance.json").write_text(json.dumps(records, indent=2), encoding="utf8")

sheet = Image.new("RGB", (920, 6*118), (41, 45, 54))
draw = ImageDraw.Draw(sheet)
for i, stem in enumerate(["original", "retro-rewind", "nsmbw", "msmbwii", "sunshine", "eclipse"]):
    image = Image.open(OUT / f"{stem}-spine.png").convert("RGB")
    row = image.transpose(Image.Transpose.ROTATE_90)
    draw.text((12, i*118+7), stem + (" (custom adaptation)" if stem=="msmbwii" else ""), fill="white")
    sheet.paste(row, (12, i*118+28))
sheet.save(OUT / "spine-contact-sheet.png")
print(json.dumps({"count":len(records), "output":str(OUT), "retail_and_community_crops_pixel_exact":True}))
