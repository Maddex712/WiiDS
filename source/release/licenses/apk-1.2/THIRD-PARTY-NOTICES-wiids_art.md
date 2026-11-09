# wiids_art: third-party notices

wiids_art builds each game's companion art on the player's device from their own disc. It is
part of WiiDS (GPL-3.0) and ships no game art.

- **gclib** (LagoLunatic, MIT): GX texture decoding rules (texture_utils.py), reproduced exactly.
- **noclip.website** (Jasper St. Pierre and contributors, MIT): BRRES/MDL0 layout, display-list
  and material register handling followed when reading course models (src/rres/brres.ts).
- **Pillow** (HPND licence): the integer alpha_composite blend is reproduced so baked layouts match.
- **MKW-3DS tools** (WiiDS workspace): menu_layout.py, race_map_bake.py and the Thor extractors
  this library ports.
