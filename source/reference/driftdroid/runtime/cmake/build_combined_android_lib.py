#!/usr/bin/env python3
"""Builds libGameCombined.so for Android: WiiCompiled + RetroRewind in one shared library.

Why this exists: on Android the two products used to ship as
separate libWiiCompiled.so/libRetroRewind.so, each independently linking its own full copy
of the shared runtime/aurora/SDL code. Live on-device profiling showed ~9% of all CPU
cycles on the hot game-loop thread going to pure cross-.so call overhead (PLT stubs +
slow cross-library thread-local access) from that split - not present when everything is
one binary. Combining them also roughly halves total .so size (measured: 727MB + 798MB
separate vs 852MB combined) since the common code is linked once instead of twice.

The obstacle: 1,341 translated PPC functions have the SAME name (they're named by guest
RAM address, e.g. func_800074D8) but a DIFFERENT compiled body between the base game and
the Retro Rewind mod - the translator's own design already keeps most code profile-neutral
and compiles only these small ABI-divergent "sensitive" variants per profile (see the
comment at the top of runtime/cmake/PublicProducts.cmake). Naively linking both profiles'
objects into one binary would either hard-fail (duplicate symbol) or silently pick one at
random. This script resolves it: every one of those 1,341 symbols is renamed (via
llvm-objcopy --redefine-syms, applied consistently to every object file in Retro Rewind's
exclusive link graph - both the function bodies AND the registration/dispatch files that
reference them by name) to a `__rr` suffix before the final link, so both versions coexist
under distinct names. Each profile's own registration table (built once per profile at
compile time, unaffected by this rename since it's applied uniformly) still only ever
points at its own version.

Desktop builds are completely unaffected: WiiCompiled.exe and RetroRewind.exe still link
independently exactly as before, with unrenamed symbols, one product per binary.

Invoked by CMake (runtime/cmake/PublicProducts.cmake, MKW_TARGET_ANDROID branch) as a
custom command depending on both the WiiCompiled and RetroRewind targets, so all the
object files this script reads have already been compiled by the time it runs.
"""
import hashlib
import os
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: build_combined_android_lib.py <build_dir> <ninja> <objcopy> <output_so>",
              file=sys.stderr)
        return 1
    build_dir, ninja, objcopy, output_so = sys.argv[1:5]

    def find_objs(root):
        result = []
        for dirpath, _dirs, filenames in os.walk(root):
            for fn in filenames:
                if fn.endswith(".o"):
                    result.append(os.path.join(dirpath, fn))
        return sorted(result)

    retro_dirs = [
        os.path.join(build_dir, "CMakeFiles/mkw_retro_sensitive.dir"),
        os.path.join(build_dir, "CMakeFiles/mkw_retro_rewind_functions.dir"),
        os.path.join(build_dir, "CMakeFiles/RetroRewind.dir"),
    ]
    retro_objs = []
    for d in retro_dirs:
        if os.path.isdir(d):
            retro_objs += [o for o in find_objs(d) if "retro_rewind_product" not in o]

    base_sensitive_dir = os.path.join(build_dir, "CMakeFiles/mkw_base_sensitive.dir")
    wiicompiled_dir = os.path.join(build_dir, "CMakeFiles/WiiCompiled.dir")
    base_objs = []
    if os.path.isdir(base_sensitive_dir):
        base_objs += find_objs(base_sensitive_dir)
    if os.path.isdir(wiicompiled_dir):
        base_objs += [
            o for o in find_objs(wiicompiled_dir)
            if "base_product.cpp.o" not in o and "combined_product.cpp.o" not in o
        ]
    # combined_product.cpp is its own dedicated CMake OBJECT target (mkw_combined_product, see
    # PublicProducts.cmake) - NOT part of WiiCompiled.dir's own object tree. A stale path here
    # (this one, pointing at WiiCompiled.dir, silently found and linked a leftover object from
    # long before mkw_combined_product existed - dated well before any of the profile-selection
    # code it's supposed to contain) fails silently: the file exists, just isn't the right one,
    # so this script has no way to detect it beyond a human noticing the wrong code ran. Confirmed
    # on-device: the process aborts here now if this ever again points at the wrong file.
    combined_product_obj = os.path.join(
        build_dir, "CMakeFiles/mkw_combined_product.dir/src/product/combined_product.cpp.o")
    if not os.path.isfile(combined_product_obj):
        print(f"error: {combined_product_obj} missing - is combined_product.cpp wired "
              "into the mkw_combined_product target (PublicProducts.cmake)?", file=sys.stderr)
        return 1

    # Colliding symbol names: functions whose compiled body differs between profiles.
    # Derived by symbol-table diff, not hand-maintained - re-derive every run so this
    # script self-corrects as the translated shard set changes across builds.
    def defined_global_text_symbols(nm, objs):
        if not objs:
            return set()
        out = subprocess.run([nm, "--defined-only", *objs], capture_output=True, text=True,
                              check=True).stdout
        names = set()
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[-2] == "T":
                names.add(parts[-1])
        return names

    nm = objcopy.replace("llvm-objcopy", "llvm-nm")
    base_syms = defined_global_text_symbols(nm, base_objs)
    retro_syms = defined_global_text_symbols(nm, retro_objs)
    colliding = sorted(s for s in (base_syms & retro_syms) if s.startswith("func_"))
    print(f"[combined-lib] {len(colliding)} colliding translated-function symbols "
          f"(base={len(base_objs)} objs, retro={len(retro_objs)} objs)")

    rename_dir = os.path.join(build_dir, "combined_retro_renamed")
    os.makedirs(rename_dir, exist_ok=True)
    map_path = os.path.join(rename_dir, "rename_map.txt")
    with open(map_path, "w") as f:
        for name in colliding:
            f.write(f"{name} {name}__rr\n")

    renamed_objs = []
    for obj in retro_objs:
        digest = hashlib.md5(obj.encode()).hexdigest()
        renamed = os.path.join(rename_dir, f"{digest}.o")
        subprocess.run([objcopy, f"--redefine-syms={map_path}", obj, renamed], check=True)
        renamed_objs.append(renamed)

    # Verify: zero collisions remain, zero dangling references to the old names.
    renamed_defined = defined_global_text_symbols(nm, renamed_objs)
    remaining = base_syms & renamed_defined
    if remaining:
        print(f"error: {len(remaining)} symbols still collide after rename: "
              f"{sorted(remaining)[:10]}", file=sys.stderr)
        return 1

    colliding_set = set(colliding)
    undef_out = subprocess.run([nm, "--undefined-only", *renamed_objs], capture_output=True,
                                text=True, check=True).stdout
    dangling = {ln.split()[-1] for ln in undef_out.splitlines()
                if ln.split() and ln.split()[-1] in colliding_set}
    if dangling:
        print(f"error: {len(dangling)} renamed objects still reference old (unrenamed) "
              f"names: {sorted(dangling)[:10]}", file=sys.stderr)
        return 1

    # Reuse RetroRewind's own real link line (has every third-party lib/flag correct) and
    # graft the object list: drop its retro-exclusive objects, add the renamed replacements
    # plus WiiCompiled's base-exclusive objects and the combined product provider.
    full_line = subprocess.run(
        [ninja, "-t", "commands", "RetroRewind"], cwd=build_dir, capture_output=True,
        text=True, check=True).stdout.strip().splitlines()[-1]
    # CMake/Ninja emits this as a shell command: a leading ": && " no-op (so the whole thing
    # chains on one shell line) followed by the actual link, then " && cd ... && cmake -E
    # copy..." asset-staging steps that WiiCompiled/RetroRewind's own builds already ran (this
    # target depends on both, so those assets are already in place). Take only the link
    # command itself and run it as an argv list, not through a shell.
    link_line = full_line.split(" && ")[1] if full_line.startswith(": && ") else full_line
    tokens = link_line.split(" ")

    def is_retro_exclusive(tok):
        return ("CMakeFiles/mkw_retro_sensitive.dir/" in tok
                or "CMakeFiles/mkw_retro_rewind_functions.dir/" in tok
                or "CMakeFiles/RetroRewind.dir/" in tok)

    kept = [t for t in tokens if not is_retro_exclusive(t)]
    new_objs = renamed_objs + base_objs + [combined_product_obj]
    insert_idx = len(kept)
    for i, t in enumerate(kept):
        if t.endswith(".a") or t.startswith("-l") or t == "-Wl,--start-group":
            insert_idx = i
            break
    kept = kept[:insert_idx] + new_objs + kept[insert_idx:]

    final = []
    skip_next = False
    out_name = os.path.basename(output_so)
    for i, t in enumerate(kept):
        if skip_next:
            skip_next = False
            continue
        if t == "-Wl,-soname,libRetroRewind.so":
            final.append(f"-Wl,-soname,{out_name}")
        elif t == "-o" and kept[i + 1] == "libRetroRewind.so":
            final.append("-o")
            final.append(output_so)
            skip_next = True
        else:
            final.append(t)

    subprocess.run(final, cwd=build_dir, check=True)
    print(f"[combined-lib] linked {output_so}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
