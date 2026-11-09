# Code in the data files

Some games keep code in their data files -- an object's setup routine beside its model,
a level's scripts in its pack -- and load it wherever their allocator puts it. A static
recompilation needs that code at build time, at a fixed address. `runtime/modules.cpp`
and the recompiler together make that work for code that is position-independent, which
is what such code has to be: it reaches the rest of the game through pointers it is
handed, and the loader relocates its data, never its instructions.

## Build time

The game project's `DOL_FIXUP` script (which also receives the disc image) extracts each
distinct module from the disc and appends them as one text section at canonical
addresses in a range the game never uses, writing `<out.dol>.modules` beside the DOL:
one `canon size entry name` line per module, in hex. The recompiler reads that file,
compiles the modules like any other code, keeps each module's bytes and the end of its
last function in the generated table (`g_recomp_modules`), and keys the jump tables of a
module's own functions on `modules_canon(c->ctr)` rather than on `c->ctr`, since a loaded
copy's addresses differ from the canonical ones. A region may hold data after the code;
only the code is matched against.

dtk needs the entries seeded (`<out.dol>.seeds`): it follows control flow from them. Its
output names every code section `.text`, so a seed has to name the section by its index
(`.text4` for the fifth text section) and the analysis has to start from the seeds alone;
its own output does not read back across duplicate names.

## Run time

Nothing is known until the game calls into a module. `call_indirect` finds no function at
the address and asks `modules_bind`: the sixteen bytes at the address are looked up among
the first sixteen bytes of every module function, each hit names a module and an offset,
and every candidate is scored by how much of its code, entry to end, the memory at the
implied base holds; the best wins if it matches nine tenths. Sixteen bytes alone are not
enough: modules open with the same prologue and share library functions, and the first
binding by prologue ran a pilot's animation setup in place of a craft's. A match is an
instance; from then on an address in it maps to the canonical one (`modules_canon`),
which the function table is keyed by. An instance is re-checked against the module's
bytes whenever it is used, so a module the game unloaded and replaced with another in
the same memory is rebound rather than misrun. `--log=os` prints each binding.

The function table is bucketed by megabyte for this (`cpu.cpp`): the recompiled code
sits in far-apart ranges, and a flat table across them would be most of a gigabyte.
