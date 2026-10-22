---
title: CodeWarrior .comment Section Internals
description: Deep-dive into the CodeWarrior ELF .comment section format, symbol alignment flags, and mwld linker behavior for GameCube/Wii.
tags:
  - decompilation
  - codewarrior
  - linker
  - mwld
  - petari
  - reverse-engineering
---

# CodeWarrior `.comment` Section

> **Acknowledgements:**
> Sourced from the [Petari Decompilation Project](https://github.com/SMGCommunity/Petari). All credits belong to the SMGCommunity reverse-engineering team.

Files built with `mwcc` contain a `.comment` section:

```
$ powerpc-eabi-readelf -We object.o

Section Headers:
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            00000000 000000 000000 00      0   0  0
  [ 1] .text             PROGBITS        00000000 000034 000708 00  AX  0   0  4
  ...
  [16] .comment          PROGBITS        00000000 00153b 0001b4 01      0   0  1
```

The `.comment` section contains information that `mwld` uses during linking, primarily symbol alignment and a "force active" / export flag.

If missing, `mwld` will **not** adjust the alignment of symbols or remove any unused symbols.

This behavior is useful when splitting our program into objects from the final post-aligned result. However, we need the `.comment` section for some purposes:

- Reproducing the [common BSS inflation bug](common_bss.md#inflation-bug) requires the `.comment` section present. The linker inflates the size of the first common BSS symbol in a TU, but won't actually move any data around unless the `.comment` section is present.
- In newer versions of the linker, using common BSS at all _without_ a valid `.comment` section will cause an internal linker error.

When the `.comment` section is generated, decomp-toolkit will mark all global symbols as "exported" to prevent any deadstripping, since the presence of the `.comment` section itself enables deadstripping.

---

## Contents

The contents of this section follow a structured format:

### Header
- `[0x0 size: 0xB]` Magic: `43 6F 64 65 57 61 72 72 69 6F 72` ("CodeWarrior")
- `[0xB size: 0x1]` Version: `XX`

### Symbol Info Table
Follows the header. Each entry defines alignment and linker flags for matching symbol entries in the ELF symbol table.

---

## References
- [Common BSS Documentation](common_bss.md)
- [Petari Repository](https://github.com/SMGCommunity/Petari)
