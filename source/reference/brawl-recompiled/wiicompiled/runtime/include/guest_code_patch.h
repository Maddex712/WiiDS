// Run-time patches to the game's translated code (Gecko codes, Syriinge plugin hooks).
//
// Once a mod that patches code is active, translated code ranges are write-trapped (RecompMod's executable guard).
// A written page is compared with its pristine bytes at the next Sync, and every translated function whose bytes
// changed runs in the interpreter from then on, through the MKW_GUEST_CODE_GUARD at its entry (abi_bridge.h).
#pragma once

#include <cstdint>

struct CpuContext;

namespace GuestCodePatch {

// Write-traps the translated code in [start, end); its current bytes are the pristine state.
void Protect(uint32_t start, uint32_t end, const char* name);
// The trap's report of a write to guarded code. False when the address is not code guarded here.
bool NoteWrite(uint32_t address);
// Compares written pages with their pristine bytes; functions whose code changed go to the interpreter.
void Sync();
// [start, end) holds its pristine code again (a module was linked again at its home).
void ResetPristine(uint32_t start, uint32_t end);
// A patch a mod made to code not guarded yet (the code handler's boot pass over a resident module): `value` is the
// word as the guest stores it.
void RecordPatch(uint32_t address, uint32_t value);
// Writes every patch known in [start, end) back over freshly linked code and syncs. On a Wii the code handler runs
// many times while a module loads, so its hooks are in place again before the module's prolog runs.
void Reapply(uint32_t start, uint32_t end);
bool Active();
void RunPatched(uint32_t address, CpuContext* ctx);
// BRAWL_INTERPRET=<hex start>-<hex end>: every translated function starting in that range runs in the interpreter, a
// check of the interpreter against the translation on the unmodded game (same behaviour, only slower). Once the
// function registry is final.
void InterpretFromEnvironment();
// A mod's replacement of a resident module links where the disc's translated functions are registered: every
// translated function in [start, end) runs the bytes there in the interpreter from now on.
void InterpretReplacedModule(uint32_t start, uint32_t end, const char* name);

} // namespace GuestCodePatch
