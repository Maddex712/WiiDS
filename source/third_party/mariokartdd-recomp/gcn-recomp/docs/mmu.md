# Page tables

Most GameCube games touch memory only through the BATs the IPL leaves behind, which map
0x80000000 and 0xC0000000 onto RAM; `HOST()` in `runtime/recomp.h` is that mapping and
nothing else. A game may also program the MMU proper -- a page table through SDR1, segment
registers, and a window below 0x80000000, which no BAT covers, that it maps page by
page, pages in on a DSI and evicts again -- and `runtime/mmu.cpp` is what makes that run.
The window the runtime translates is 0x02000000-0x7FFFFFFF; below it is physical RAM,
which the runtime's own DMA and page table walk address directly, and which no game's
virtual addresses use. The comment at the top of that file is the design; this is what to know when using
or extending it.

## How it works

An access to the window takes a slow path in `HOST()`: a compare and a never-taken branch
for every other address. `vm_translate` keeps a software TLB, one entry per page of the
window, direct-mapped and tagged by page number, and on a miss reads the game's page
table with the 750's hashed lookup (primary hash, then secondary) under the segment
register for the address.

- A valid PTE whose protection allows the access fills the TLB entry and returns the
  physical address. The PTE's R bit is set, and C on a write; a page whose PTE allows
  writes but whose C bit is clear is entered read-only until the first write misses on
  it, so a game that reads C to decide whether an evicted page needs writing back sees
  what the hardware would have set. `HOST_W()` is the store form; the runtime's own
  writers (`mem_w*`, `mem_ptr`, DMA) go through it.
- No PTE, or a protection violation, is a DSI for the game, delivered right there on the
  guest thread. DAR, DSISR, SRR0 and SRR1 are set as the hardware sets them, MSR cleared
  as on entry, and the vector at 0x300 runs. That vector is whatever the game wrote there
  at boot, which the recompiler never saw, so a small interpreter (`vector_run`) steps
  through it until it branches to a function the recompiler did see, which runs
  natively; a `bl` or `blrl` into such a function returns to the interpreter, a plain
  branch ends the exception when the function returns. SRR0 holds the link register
  rather than a pc, and `hle_rfi` treats an rfi to the link register as a return, which
  is how the handler's rfi ends the exception. The lookup is then made again.
- `tlbie` (`hle_tlbie`) drops the TLB entry, so a remapped or evicted page is looked up
  afresh. The segment registers are kept by `hle_mtsr`/`hle_mfsr` and SDR1 by
  `hle_mtspr`; a change to either flushes the TLB.

A DSI can only be delivered on a guest thread. A runtime thread (the GX front end, DVD)
that touches an unmapped page is a crash, with a message saying so.

`--log=os` prints every DSI delivered.

## What did not work

The first design aliased the host pages instead, so that the fast path served the window
too: on a fault, `mach_vm_remap` the physical page's host page at the window's host
address and retry. It cannot work: the host's page is 16 KB on Apple silicon and the
guest's is 4 KB, so one guest page cannot be mapped on its own, and the three neighbours
it dragged along were served from the wrong physical pages. It looked like it worked for
a while, because a game's first pages happen to be contiguous.

## Analysis side

Such a game's executable may not be laid out the way it runs: the window's contents can be
a second image carried inside the DOL and paged into place at boot. The recompiler needs
that code at its run-time addresses, so `gcn_add_game(DOL_FIXUP <script>)` runs a
game-project script over the extracted DOL and recompiles the result; the runtime still
boots the original. The script, and the layout it knows, belong to the game project.
