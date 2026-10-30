"""Every Brawl .rel module at a fixed home address, pre-linked, as one synthetic REL for the base translation.

Each of the 126 modules (files/module/*.rel) gets its own home in MEM2 above the 64 MiB the game sees (0x94004000 up),
so modules the game loads at the same heap address (all stages, all fighters) never share code addresses. Every
module is relocated for its home with the imports from the dol and from every other module resolved, so the whole
set translates together with main.dol. At run time hle/brawl_modules.cpp links a module by copying its pristine
pre-linked image to its home instead of relocating it in the game's buffer.

The modules Brawl loads once at boot into its OverlayCommon heap (RESIDENT) are the exception: they always land at
the same addresses, Gecko codes (Project+) patch them there, so they are pre-linked and translated at those addresses,
through an extra text section in a translation-only copy of main.dol.

Usage: build_modules.py <files/module dir> <decomp config/RSBE01_02 dir> <out .rel> <wiicompiled dir> <main.dol>
Writes: <out .rel>, <out dir>/main_translate.dol, projects/brawl/MAP_modules.txt (function starts),
projects/brawl/module_layout.json, runtime/src/hle/brawl_module_layout.inc (the runtime's table)."""
import json, re, struct, sys, zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from prelink_rels import Rel, link

HOME_BASE = 0x94000000        # synthetic REL load address
FIRST_HOME = 0x94004000       # room for the synthetic header and section table
HOME_LIMIT = 0x97C00000       # the runtime maps MEM2 to 0x98000000; the game's arena ends at 0x94000000
# Loaded at boot into OverlayCommon and never unloaded: (image, bss) where the game puts them.
# (A retail console's MEM1 arena starts at 0x805B5160; these move with it.)
RESIDENT = {
    "sora_menu_name": (0x80673FA0, 0x80680AA0),
    "sora_menu_sel_char_access": (0x80680C40, 0x80682720),
    "sora_menu_sel_char": (0x80682800, 0x806A3960),
    "sora_menu_rule": (0x806A3A60, 0x806B07E0),
    "sora_menu_sel_stage": (0x806B08C0, 0x806BB380),
    "sora_scene": (0x806BB480, 0x80708C40),
    "sora_melee": (0x8070A940, 0x80B84DC0),
}
# Their calls into on-demand modules (at homes, out of rel24 reach) go through veneers at these addresses, within reach
# of OverlayCommon. Nothing executes there: the translator folds each veneer into a direct call, and the interpreter
# reaches the translated veneer through the function registry. It is sora_melee's bss (36 KiB, resident for good), so
# no code a mod loads can ever sit at a veneer's address and be mistaken for it.
RESIDENT_VENEERS = RESIDENT["sora_melee"][1]
# The Gecko code handler (projects/brawl/gecko), translated with the game; its entry is CODE_HANDLER + 0xA8.
CODE_HANDLER = 0x80001800


def align(v, a=0x20):
    return (v + a - 1) & ~(a - 1)


def rel24_imports(m, from_ids):
    """How many rel24 relocations m has against the modules in from_ids: an upper bound on its veneers to them."""
    count = 0
    for k in range(m.imp_size // 8):
        imp_mod, ro = struct.unpack(">II", m.raw[m.imp_off + 8 * k:m.imp_off + 8 * k + 8])
        c = ro
        while imp_mod in from_ids:
            _d, typ, _s, _add = struct.unpack(">HBBI", m.raw[c:c + 8])
            c += 8
            if typ == 203:
                break
            count += typ == 10
    return count


def fix_size(m):
    return struct.unpack(">I", m.raw[0x48:0x4C])[0] if m.ver >= 3 else len(m.raw)


def main():
    mod_dir, cfg_dir, out_rel, wc, dol_path = map(Path, sys.argv[1:6])
    mods = sorted((Rel(p) for p in mod_dir.glob("*.rel")), key=lambda m: m.id)
    assert len({m.id for m in mods}) == len(mods), "duplicate module ids"
    addr = FIRST_HOME
    resident_veneers = RESIDENT_VENEERS
    for m in mods:
        if m.name in RESIDENT:
            m.place(*RESIDENT[m.name], resident_veneers)
            resident_veneers += 16 * rel24_imports(m, {r.id for r in mods if r.name not in RESIDENT})
            continue
        image = addr
        bss = align(image + fix_size(m))
        veneers = align(bss + max(m.bss_size, 4))
        # Out of rel24 reach from a home: main.dol, and the resident modules down in MEM1.
        far = len(m.far_call_targets()) + rel24_imports(m, {r.id for r in mods if r.name in RESIDENT})
        addr = align(veneers + 16 * far)
        m.place(image, bss, veneers)
    assert addr <= HOME_LIMIT, hex(addr)
    melee = next(m for m in mods if m.name == "sora_melee")
    assert resident_veneers <= RESIDENT_VENEERS + melee.bss_size, "resident veneers overflow sora_melee's bss"
    linked = link({m.id: m for m in mods}, 0)
    for i, m in enumerate(mods):
        if m.name not in RESIDENT and m.veneers:
            used_end = m.veneer_base + 16 * len(m.veneers)
            nxt = next((n.image for n in mods[i + 1:] if n.name not in RESIDENT), addr)
            assert used_end <= nxt, f"{m.name}: veneers run to 0x{used_end:08X}, past the next home 0x{nxt:08X}"

    # The synthetic REL: the home region as one image, every module section as a section at its absolute offset.
    end = addr
    mem = bytearray(end - HOME_BASE)
    sections = [(0, False, 0)]
    for m in mods:
        if m.name in RESIDENT:
            continue
        base, image, _unresolved = linked[m.name]
        n = fix_size(m)
        mem[m.image - HOME_BASE:m.image - HOME_BASE + n] = image[m.image - base:m.image - base + n]
        for i, (o, e, s) in enumerate(m.secs):
            if i == 0 or not s:
                continue
            sections.append((m.sec_addr(i) - HOME_BASE, e, s))
        if m.veneers:
            v = 16 * len(m.veneers)
            mem[m.veneer_base - HOME_BASE:m.veneer_base - HOME_BASE + v] = image[m.veneer_base - base:m.veneer_base - base + v]
            sections.append((m.veneer_base - HOME_BASE, True, v))
    hdr_size = 0x4C + 8 * len(sections)
    assert hdr_size <= FIRST_HOME - HOME_BASE, hex(hdr_size)
    size = len(mem)
    hdr = struct.pack(">IIIIIIIIIIIIBBBBIII", 0x7E, 0, 0, len(sections), 0x4C, 0, 0, 3, 0, size, size, 0, 0, 0, 0, 0,
                      0, 0, 0) + struct.pack(">III", 0x20, 0x20, size)
    mem[0:hdr_size] = hdr + b"".join(struct.pack(">II", a | (1 if e else 0), s) for a, e, s in sections)
    out_rel.write_bytes(mem)

    # The resident modules as one more text section of main.dol (translation only; the game loads them itself).
    lo = min(RESIDENT[n][0] for n in RESIDENT)
    hi = max(m.image + fix_size(m) for m in mods if m.name in RESIDENT)
    span = bytearray(align(hi - lo))
    veneer_span = bytearray(align(max(resident_veneers - RESIDENT_VENEERS, 4)))
    for m in mods:
        if m.name in RESIDENT:
            base, image, _unresolved = linked[m.name]
            n = fix_size(m)
            span[m.image - lo:m.image - lo + n] = image[m.image - base:m.image - base + n]
            if m.veneers:
                v = 16 * len(m.veneers)
                veneer_span[m.veneer_base - RESIDENT_VENEERS:m.veneer_base - RESIDENT_VENEERS + v] =                     image[m.veneer_base - base:m.veneer_base - base + v]
    dol = bytearray(dol_path.read_bytes())
    h = list(struct.unpack(">18I18I18IIII", dol[:0xE4]))
    handler = (wc / "projects/brawl/gecko/codehandler.bin").read_bytes()
    for at, blob in ((lo, span), (RESIDENT_VENEERS, veneer_span), (CODE_HANDLER, handler)):
        slot = next(i for i in range(7) if h[36 + i] == 0)
        dol = dol + bytes(align(len(dol)) - len(dol))
        h[slot], h[18 + slot], h[36 + slot] = len(dol), at, len(blob)
        dol = dol + blob
    dol[:0xE4] = struct.pack(">18I18I18IIII", *h)
    (out_rel.parent / "main_translate.dol").write_bytes(dol)
    # Everything in those text sections that is not code: the resident modules' data and the handler's variables.
    code = sorted((m.sec_addr(i), m.sec_addr(i) + sz) for m in mods if m.name in RESIDENT
                  for i, (_o, e, sz) in enumerate(m.secs) if i and e and sz)
    data, at = [(CODE_HANDLER, CODE_HANDLER + 0xA8)], lo
    for code_start, code_end in code:
        if code_start > at:
            data.append((at, code_start))
        at = max(at, code_end)
    data.append((at, lo + len(span)))
    (wc / "projects/brawl/resident_data_ranges.txt").write_text(
        "# Generated by build_modules.py: data inside main_translate.dol's text sections (translator --data-in-text)\n"
        + "".join(f"{a:08X} {b:08X}\n" for a, b in data))

    # Function starts from the decomp's per-module symbols (section-relative), at the homes.
    funcs = []
    for m in mods:
        sym = cfg_dir / "rels" / m.name / "symbols.txt"
        splits = cfg_dir / "rels" / m.name / "splits.txt"
        names = [l.split()[0] for l in splits.read_text().split("Sections:")[1].split("\n\n")[0].splitlines() if l.strip()]
        index = {n: i + 1 for i, n in enumerate(names)}  # REL section index by the order splits.txt lists them
        for l in sym.read_text().splitlines():
            r = re.match(r"(\S+) = (\.\w+):0x([0-9A-Fa-f]+); // type:function", l)
            if r and r[2] in index and m.secs[index[r[2]]][1]:
                funcs.append((m.sec_addr(index[r[2]]) + int(r[3], 16), f"{m.name}_{r[1]}"))
        funcs += [(at, f"{m.name}_veneer_{t:08X}") for t, at in m.veneers.items()]
    funcs.append((CODE_HANDLER + 0xA8, "gecko_codehandler"))
    funcs.sort()
    (wc / "projects/brawl/MAP_modules.txt").write_text("".join(f"{a:08X} {n}\n" for a, n in funcs))
    # The translator takes one function map: main.dol's (MAP.txt) and the modules'.
    (wc / "projects/brawl/MAP_full.txt").write_text((wc / "projects/brawl/MAP.txt").read_text() +
                                                    (wc / "projects/brawl/MAP_modules.txt").read_text())

    layout = {}
    rows = []
    for m in mods:
        n = fix_size(m)
        # The unrelocated file after its 0x4C-byte header (the game rewrites relOffset/impOffset to tables it keeps
        # elsewhere): a modded module differs.
        crc = zlib.crc32(m.raw[0x4C:n]) & 0xFFFFFFFF
        layout[m.name] = {"id": m.id, "image": hex(m.image), "bss": hex(m.bss), "bss_size": hex(m.bss_size),
                          "veneers": hex(m.veneer_base), "veneer_count": len(m.veneers),
                          "fix_size": hex(n), "crc32": hex(crc)}
        resident = "true" if m.name in RESIDENT else "false"
        rows.append(f'    {{{m.id}u, 0x{m.image:08X}u, 0x{m.bss:08X}u, 0x{m.bss_size:X}u, 0x{n:X}u, 0x{crc:08X}u, "{m.name}", {resident}}},')
    (wc / "projects/brawl/module_layout.json").write_text(json.dumps(layout, indent=1))
    (wc / "runtime/src/hle/brawl_module_layout.inc").write_text(
        "// Generated by projects/brawl/tools/build_modules.py: every Brawl module's home (pre-linked image and bss).\n"
        "// {id, image, bss, bss size, fixed image size, crc32 of the unrelocated file from 0x4C to the fixed size, name,\n"
        "//  resident: loaded at boot at these addresses, the image being the game's own buffer}\n"
        + "\n".join(rows) + "\n")
    print(f"{len(mods)} modules, homes 0x{FIRST_HOME:08X}..0x{end:08X} ({(end - HOME_BASE) / 1048576:.1f} MiB), "
          f"{len(sections) - 1} sections, {len(funcs)} functions")


if __name__ == "__main__":
    main()
