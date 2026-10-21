# Printed launcher spines

This prepares existing printed case artwork for the projected launcher spine. It does not replace any case front, disc or plastic color.

Five source images are pinned by URL, SHA-256, byte length and dimensions in `scans.json`: retail Mario Kart Wii, New Super Mario Bros. Wii and Super Mario Sunshine; community custom Retro Rewind and Super Mario Eclipse. GameTDB hosts the public full-cover PNGs. These are image assets only, not game downloads. The mod covers are not Nintendo retail packaging.

MSMBWii is a documented custom adaptation because no existing matching full-cover spine was found. Its real red Wii spine/header/footer come from the NSMB scan, and its title/subtitle pixels come from the existing owner-approved `build/thor-map-data/launcher/msmbwii-cover.png`. No new title font is drawn.

Run from the workspace with Python and Pillow:

```powershell
python experiments/launcher-spines/prepare_spines.py
# Explicitly fetch missing public images, bounded to 5 MiB each and hash checked:
python experiments/launcher-spines/prepare_spines.py --download
```

The default requires the five already downloaded inputs in `build/case-spines/scans`. Inputs are validated before use. Downloads verify PNG format, exact dimensions/size/hash before publishing the local filename atomically. The existing owner-approved MSMBWii cover is required separately; this tool never downloads or modifies that cover.

Outputs are in `build/case-spines/spines`, with a readable rotated contact sheet and `spine-provenance.json`. The five scan crops are exact original pixels, with no resizing or recoloring:

| Output | Crop (left, top, right exclusive, bottom exclusive) | PNG dimensions |
|---|---|---|
| original-spine.png | 484, 0, 536, 680 | 52 × 680 |
| nsmbw-spine.png | 484, 0, 536, 680 | 52 × 680 |
| sunshine-spine.png | 489, 0, 540, 680 | 51 × 680 |
| retro-rewind-spine.png | 478, 0, 536, 680 | 58 × 680 |
| eclipse-spine.png | 485, 0, 539, 680 | 54 × 680 |
| msmbwii-spine.png | Adaptation recipe in provenance | 52 × 680 |

All images are upright portrait PNGs, exceeding the loader's 16-pixel minimum edge. Image top is the physical case top; source lettering reads downwards. GameCube branding remains at its scanned physical-bottom position. Retro Rewind's existing community title slightly overlaps its nominal fold; its wider crop preserves the complete printed logo.

Keep the existing plain-label fallback if an optional spine is missing or fails runtime validation. This preparation tool does not change the runtime, staged assets, device, APK or public release. Visually check the generated contact sheet and actual projected case before shipping.
