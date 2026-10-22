---
title: Decompilation splits.txt Specification
description: Syntax and specification for splits.txt, configuring object file splits, section boundaries, and linker flags for GameCube/Wii DOLs and RELs.
tags:
  - decompilation
  - splits
  - dol
  - rel
  - petari
  - reverse-engineering
---

# `splits.txt` Specification

> **Acknowledgements:**
> Sourced from the [Petari Decompilation Project](https://github.com/SMGCommunity/Petari). All credits belong to the SMGCommunity reverse-engineering team.

This file defines the binary boundary splits for compiling modular translation units from the original executable.

```yaml
path/to/file.cpp:
	.text       start:0x80047E5C end:0x8004875C
	.ctors      start:0x803A54C4 end:0x803A54C8
	.data       start:0x803B1B40 end:0x803B1B60
	.bss        start:0x803DF828 end:0x803DFA8C
	.bss        start:0x8040D4AC end:0x8040D4D8 common
```

---

## Format

```yaml
path/to/file.cpp: [file attributes]
    section     [section attributes]
    ...
```

- `path/to/file.cpp`: Source file relative to `src`. Corresponds to compiler configuration in `configure.py`.

### File Attributes

- `comment:` Overrides the `mw_comment_version` setting for this file. See [Comment section](comment_section.md).
  - `comment:0` disables `.comment` generation for non-`mwcc` files (e.g. assembled `.s` files).
- `order:` Explicitly dictates link order index where automatic detection is ambiguous.

### Section Attributes

- `start:` Start address of the section within the file (absolute for DOL, relative for REL).
- `end:` End address of the section within the file.
- `align:` Section alignment requirement.
- `rename:` Renames the section when generating the object (e.g. `.ctors$10`).
- `common:` Identifies common BSS symbols. See [Common BSS](common_bss.md).
- `skip:` Ignores linker-generated data blocks.

---

## References
- [Getting Started](getting_started.md)
- [Symbols Guide](symbols.md)
- [Petari Repository](https://github.com/SMGCommunity/Petari)
