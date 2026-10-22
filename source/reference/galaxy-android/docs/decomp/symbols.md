---
title: Decompilation symbols.txt Specification
description: Syntax and specification for symbols.txt, defining symbol addresses, types, sizes, visibility, and compiler attributes.
tags:
  - decompilation
  - symbols
  - demangling
  - attributes
  - petari
  - reverse-engineering
---

# `symbols.txt` Specification

> **Acknowledgements:**
> Sourced from the [Petari Decompilation Project](https://github.com/SMGCommunity/Petari). All credits belong to the SMGCommunity reverse-engineering team.

This file defines symbol mappings and attributes for binary modules.

```
__dt__13mDoExt_bckAnmFv = .text:0x800DD2EC; // type:function size:0x5C scope:global align:4
```

---

## Format

```
symbol_name = section:address; // [attributes]
```

- `symbol_name`: Mangled symbol name.
- `section`: ELF section name (`.text`, `.data`, `.rodata`, `.bss`, etc.).
- `address`: Hexadecimal or decimal memory address.

### Attributes

- `type:` Function, object, or label.
- `size:` Size in bytes.
- `scope:` Symbol visibility: `global`, `local`, or `weak`.
- `align:` Memory alignment boundary.
- `data:` Data type for disassembly formatting (`float`, `double`, `int`, `4byte`, `string`, etc.).
- `force_active:` Prevents the symbol from being deadstripped by `mwld`.
- `noreloc:` Prevents non-pointer values from being interpreted as relocation targets.

---

## References
- [Getting Started](getting_started.md)
- [Splits Guide](splits.md)
- [Comment Section](comment_section.md)
- [Petari Repository](https://github.com/SMGCommunity/Petari)
