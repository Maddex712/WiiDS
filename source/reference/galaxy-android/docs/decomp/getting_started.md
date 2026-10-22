---
title: SMG Decompilation - Getting Started
description: Guide for setting up and understanding the reverse-engineering and decompilation toolchain.
tags:
  - decompilation
  - petari
  - reverse-engineering
  - wii
  - symbols
  - splits
---

# SMG Decompilation: Getting Started

> **Acknowledgements:**
> This reference documentation is preserved directly from the [Petari Decompilation Project](https://github.com/SMGCommunity/Petari). All credit belongs to the SMGCommunity reverse-engineering team.

See [Dependencies](dependencies.md) first.

1. Clone the decompilation tree.
2. Extract your game to `orig/[GAMEID]`. In Dolphin, use "Data Partition" -> "Extract Entire Partition" for Wii games.
3. Configure `config/[GAMEID]/config.yml` appropriately. If the game doesn't use RELs, the `modules` list in `config.yml` can be removed.
4. Generate a `config/[GAMEID]/build.sha1` file for verification.
5. Modify the paths in `config/[GAMEID]/build.sha1` to point to the `build` directory instead of `orig`.
6. Update `VERSIONS` in `configure.py` with the game ID.
7. Run `python configure.py` to generate the initial `build.ninja`.
8. Run `ninja` to perform initial analysis.

---

## Using a `.map`

If the game has `.map` files matching the DOL (and RELs, if applicable), they can be used to fill out `symbols.txt` and `splits.txt` automatically during initial analysis.

Add the `map` key to `config.yml`, pointing to the `.map` file from the game disc (for example, `orig/[GAMEID]/files/main.map`). For RELs, add a `map` key to each module in `config.yml`.

If the game uses [common BSS](common_bss.md), be sure to set `common_start` as well. Otherwise, the final link order may fail to be determined.

Once the initial analysis is completed, `symbols.txt` and `splits.txt` will be generated from the map information.

---

## Post-analysis

After initial analysis, `symbols.txt` and `splits.txt` will be generated. These files can be modified to adjust symbols and split points.

If the game uses C++ exceptions, set up a split for the `__init_cpp_exceptions.cpp` file:

```yaml
# splits.txt
Runtime.PPCEABI.H/__init_cpp_exceptions.cpp:
	.text       start:0x80345C34 end:0x80345CA4
	.ctors      start:0x803A54A0 end:0x803A54A4 rename:.ctors$10
	.dtors      start:0x803A56A0 end:0x803A56A4 rename:.dtors$10
	.dtors      start:0x803A56A4 end:0x803A56A8 rename:.dtors$15
	.sdata      start:0x80418CA8 end:0x80418CB0
```

- **`.text`:** Find `__fini_cpp_exceptions` and `__init_cpp_exceptions` in `symbols.txt`. The split end is the address of `__init_cpp_exceptions` + size.
- **`.ctors$10`:** Find the address of `__init_cpp_exception_reference` or `_ctors` in `symbols.txt` (size 4).
- **`.dtors$10` / `.dtors$15`:** Look for `__destroy_global_chain_reference` and `__fini_cpp_exceptions_reference` in `symbols.txt`.
- **`.sdata`:** Find `fragmentID` in `symbols.txt`.

## References
- [Petari Repository](https://github.com/SMGCommunity/Petari)
- [Symbols Guide](symbols.md)
- [Splits Guide](splits.md)
