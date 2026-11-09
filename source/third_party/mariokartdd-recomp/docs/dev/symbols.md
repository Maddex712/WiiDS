# Symbols

`analysis/symbols.txt` started as decomp-toolkit's own analysis of the retail DOL
(`tools/analyze.sh`): 13,434 functions, the SDK's named by dtk's signatures, the rest
`fn_<address>`.

The disc also carries a linker map, `debugInfoS.MAP`, of an earlier build (the same one the
MKDD decompilation targets, "MarioClub_us"). Its addresses are not the retail DOL's -- its
`.text` is 0x39B000 bytes to the retail 0x2D2E60, and `OSSaveContext` is at 0x800F98D4
there and 0x800E7998 here -- but its link order is, and most functions are the same size.
`tools/name_from_map.py` aligns the two function lists on size, in order (difflib), takes
runs of two or more agreeing sizes and the equal-length gaps between them whose sizes are
within a quarter of each other, and renames the `fn_` entries. On the first run it named
7,840 functions; of the 292 it paired with a function dtk had already named, 291 agreed.

To redo it (the map is game data, so it is not in the repository):

```sh
python3 -c "import sys; sys.path.insert(0,'gcn-recomp/recomp'); from dol import DiscImage; \
from rel import disc_files, read_disc_file; i=DiscImage(sys.argv[1]); \
open('build/debugInfoS.MAP','wb').write(read_disc_file(i, disc_files(i), 'debugInfoS.MAP'))" rom/*.ciso
tools/name_from_map.py build/debugInfoS.MAP analysis/symbols.txt
tools/analyze.sh
```

Data is not aligned (the map's data sizes differ more); globals are named by hand when they
are found, e.g. `RaceMgr::sRaceManager` and `PauseManager::mIsPause` (`vr.md`). The
decompilation's headers (CC0) give the classes' layouts; its addresses are the other build's.
