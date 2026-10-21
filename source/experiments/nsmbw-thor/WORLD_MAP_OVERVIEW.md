# Native world overview interface

The Thor lower map is a read-only projection of the real active Wii world graph
onto X/Z. It does not crop or use the upper camera. The map player marker uses
the actual map actor's changing X/Z position. Bounds derive from graph points,
so the overview remains stable as the player walks. Navigation remains with the
physical game controls; this interface supplies no touch navigation or writes.

`ThorWorldMap::Capture()` runs only at a guest-thread frame boundary. `ReadWords()`
copies the mutex-protected publication for JNI; UI threads never read guest RAM.
The ABI is documented in `runtime/include/thor_world_map.h`: 12 header words and
12 words per used point, retaining original point IDs and links. Float values
are IEEE 754 bit patterns in int32 words. Header flags 1/2 denote graph/player
availability independently. Consumers must gate the marker on bit 2, treat a
zero-extent map safely, and clear old content when the graph flag is absent.

The existing E1 `world_map_nav.cpp` supplies layout evidence: map instance
`0x8042A18C`, submap index `+0x338C`, four connect objects at `+0x5C7F4` with
stride `0x3F08`, 192 points of `0x54` bytes starting at connect `+4`. Each point
has its five-character name at `+4`, XYZ at `+0xC`, and six directional pointers
at `+0x18`. Player instance `0x8042A1A0` uses actor position `+0xAC`.

Selected functions were additionally extracted from the owner's verified clean
E1 translation source bundle into ignored `build/nsmbw-thor/e1/world-overview-audit`:
`getWorldNo` (`0x800105B0`) directly reads SDA1 minus 21555, byte `0x8042A24D`.
`daWmMap::GetPos` (`0x80100200`) confirms the submap/connect calculation;
`dPnt::init` (`0x800F2B60`) confirms empty name and cleared directional pointers.
No derived game source belongs in public documentation or patches.

Only node identity and topology are exposed. Nodes whose name begins W are
stops; others are intermediate path points. Neither node color nor the links
claim completion, unlocking, or permission to travel. A source-proven save/type
mapping would be required before adding those states. Invalid pointers, names,
non-finite/excessive coordinates and links to unused/misaligned points fail
closed. Scene 3 is the sole graph-capture scene; other scenes clear the graph.

`tests/Test-ThorWorldMap.ps1` compiles the actual C++ against a read-only Memory
mock and runs it in a temporary ARM64 shell test, with no Activity or input
operations. Its receipt is `build/nsmbw-thor/e1/world-map-test/verification.json`.
This verifies parsing/publication behavior, not live gameplay or exact visual
parity with NSMB2. Device world-map comparison remains a separate check.
