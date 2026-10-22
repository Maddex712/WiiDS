---
title: Common BSS Decompilation Guide
description: Explanation of CodeWarrior -common on behavior, common symbol allocation in .bss, link order recovery, and detection techniques.
tags:
  - decompilation
  - bss
  - codewarrior
  - symbols
  - petari
  - mwcc
---

# Common BSS

> **Acknowledgements:**
> Sourced from the [Petari Decompilation Project](https://github.com/SMGCommunity/Petari). All credits belong to the SMGCommunity reverse-engineering team.

When passed the `-common on` flag, `mwcc` will generate global BSS symbols as **common**. The linker deduplicates common symbols with the same name, and allocates an area at the **end** of `.bss` for them.

This is a legacy feature, allowing uninitialized global variables to be defined in headers without linker errors:

```c
// foo.h
int foo;
```

With `-common on`, any TU that includes `foo.h` will define `foo` as a **common** symbol. The linker will deduplicate `foo` across TUs, similar to weak symbols. Common symbols are then generated at the **end** of `.bss`, after all other `.bss` symbols.

In `splits.txt`, common BSS can be defined with the `common` attribute:

```yaml
foo.cpp:
	.text       start:0x80047E5C end:0x8004875C
	.ctors      start:0x803A54C4 end:0x803A54C8
	.data       start:0x803B1B40 end:0x803B1B60
	.bss        start:0x803DF828 end:0x803DFA8C
	.bss        start:0x8040D4AC end:0x8040D4D8 common
```

Marking common `.bss` appropriately is critical for determining the final link order.

---

## Detection

Examine symbols in map files:
```
00016e60 00000c 805069c0  1 .bss 	utilityU.a PSMainSide_CreaturePrm.cpp
00016e60 00000c 805069c0  4 @3464 	utilityU.a PSMainSide_CreaturePrm.cpp
00016e6c 000048 805069cc  4 saoVVOutput_direction___Q214JStudio_JStage14TAdaptor_light 	JSystem.a object-light.cpp
00016eb4 0000d0 80506a14  4 saoVVOutput___Q214JStudio_JStage14TAdaptor_actor 	JSystem.a object-actor.cpp
```

A file close to the end of link order followed by an object from early in the link order indicates the boundary where common BSS begins.

---

## References
- [Getting Started](getting_started.md)
- [Comment Section](comment_section.md)
- [Petari Repository](https://github.com/SMGCommunity/Petari)
