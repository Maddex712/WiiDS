"""Offline OSLink for Brawl's .rel modules: each module relocated at the image/bss address the game links it at.
Imports from the dol (module 0), from itself and from any other module in the layout are resolved; the result is the
module's memory image as it looks right after OSLink, plus a JSON layout the translator and runtime read.

Usage: prelink_rels.py <files/module dir> <layout.json> <out dir>
layout.json: {"sora_scene": {"image": "0x80...", "bss": "0x80..."}, ...}  (from the runtime's "REL module" log lines)
Output: <out dir>/<name>.bin (image .. end of bss, relocated) and <out dir>/rel_layout.json (sections, prolog, ...)."""
import json, struct, sys
from pathlib import Path

R_PPC = {0: "none", 1: "addr32", 2: "addr24", 3: "addr16", 4: "addr16_lo", 5: "addr16_hi", 6: "addr16_ha",
         7: "addr14", 8: "addr14_brtaken", 9: "addr14_brntaken", 10: "rel24", 11: "rel14", 12: "rel14_brtaken",
         13: "rel14_brntaken", 201: "nop", 202: "section", 203: "end"}


class Rel:
    def __init__(self, path):
        self.name = path.stem
        self.raw = path.read_bytes()
        (self.id, _n, _p, self.nsec, self.secoff, _no, _ns, self.ver, self.bss_size, self.rel_off, self.imp_off,
         self.imp_size, self.prolog_sec, self.epilog_sec, self.unres_sec, self.bss_sec, self.prolog, self.epilog,
         self.unres) = struct.unpack(">IIIIIIIIIIIIBBBBIII", self.raw[:0x40])
        self.secs = []
        for i in range(self.nsec):
            o, s = struct.unpack(">II", self.raw[self.secoff + 8 * i:self.secoff + 8 * i + 8])
            self.secs.append((o & ~1, bool(o & 1), s))
        # The bss section is the one with a size but no file offset.
        self.bss_index = next((i for i, (o, e, s) in enumerate(self.secs) if o == 0 and s and i), 0)

    def place(self, image, bss, veneers=None):
        self.image, self.bss = image, bss
        self.veneer_base = veneers  # long-branch veneers for calls out of rel24 range (see far_call_targets)
        self.veneers = {}

    def far_call_targets(self):
        """Distinct main.dol addresses this module reaches with rel24 branches. Placed outside +/-32 MiB of the dol,
        each call goes through a veneer (lis/addi r12, mtctr r12, bctr) placed after the module's bss."""
        out = set()
        for k in range(self.imp_size // 8):
            imp_mod, ro = struct.unpack(">II", self.raw[self.imp_off + 8 * k:self.imp_off + 8 * k + 8])
            c = ro
            while imp_mod == 0:
                _d, typ, _s, add = struct.unpack(">HBBI", self.raw[c:c + 8])
                c += 8
                if typ == 203:
                    break
                if typ == 10:
                    out.add(add)
        return sorted(out)

    def veneer(self, target):
        if target not in self.veneers:
            assert self.veneer_base is not None, f"{self.name}: no veneer area for 0x{target:08X}"
            self.veneers[target] = self.veneer_base + 16 * len(self.veneers)
        return self.veneers[target]

    def sec_addr(self, i):
        o, _e, _s = self.secs[i]
        return self.bss if i == self.bss_index else self.image + o

    def end(self):
        veneers = (self.veneer_base or 0) + 16 * len(self.far_call_targets()) if self.veneer_base else 0
        return max(self.image + len(self.raw), self.bss + self.bss_size, veneers)


def link(mods_by_id, dol_end):
    out = {}
    for m in mods_by_id.values():
        base = min(m.image, m.bss)
        mem = bytearray(m.end() - base)
        mem[m.image - base:m.image - base + len(m.raw)] = m.raw

        def rd32(a): return struct.unpack(">I", mem[a - base:a - base + 4])[0]
        def wr32(a, v): mem[a - base:a - base + 4] = struct.pack(">I", v & 0xFFFFFFFF)
        def wr16(a, v): mem[a - base:a - base + 2] = struct.pack(">H", v & 0xFFFF)
        unresolved = set()
        for k in range(m.imp_size // 8):
            imp_mod, ro = struct.unpack(">II", m.raw[m.imp_off + 8 * k:m.imp_off + 8 * k + 8])
            src = mods_by_id.get(imp_mod) if imp_mod else None
            if imp_mod and src is None:
                unresolved.add(imp_mod)  # imports from a module not in this layout stay unrelocated (OSLink does the same)
                continue
            c, cur = ro, 0
            while True:
                delta, typ, ssec, add = struct.unpack(">HBBI", m.raw[c:c + 8])
                c += 8
                if typ == 203:
                    break
                if typ == 202:
                    cur = m.sec_addr(ssec)
                    continue
                cur += delta
                if typ in (0, 201):
                    continue
                tgt = (add if imp_mod == 0 else src.sec_addr(ssec) + add) & 0xFFFFFFFF
                d = cur
                if typ == 1: wr32(d, tgt)
                elif typ == 2: wr32(d, (rd32(d) & ~0x03FFFFFC) | (tgt & 0x03FFFFFC))
                elif typ in (3, 4): wr16(d, tgt)
                elif typ == 5: wr16(d, tgt >> 16)
                elif typ == 6: wr16(d, (tgt >> 16) + (1 if tgt & 0x8000 else 0))
                elif typ in (7, 8, 9): wr32(d, (rd32(d) & ~0xFFFC) | (tgt & 0xFFFC))
                elif typ == 10:
                    if not -0x2000000 <= tgt - d < 0x2000000:
                        tgt = m.veneer(tgt)
                    wr32(d, (rd32(d) & ~0x03FFFFFC) | ((tgt - d) & 0x03FFFFFC))
                elif typ in (11, 12, 13):
                    assert -0x8000 <= tgt - d < 0x8000, f"{m.name}: rel14 at 0x{d:08X} cannot reach 0x{tgt:08X}"
                    wr32(d, (rd32(d) & ~0xFFFC) | ((tgt - d) & 0xFFFC))
                else:
                    sys.exit(f"{m.name}: unhandled relocation type {typ}")
        for tgt, at in m.veneers.items():
            lo = tgt & 0xFFFF
            ha = ((tgt >> 16) + (1 if lo & 0x8000 else 0)) & 0xFFFF
            for i, w in enumerate((0x3D800000 | ha, 0x398C0000 | lo, 0x7D8903A6, 0x4E800420)):  # lis/addi r12, mtctr, bctr
                wr32(at + 4 * i, w)
        out[m.name] = (base, bytes(mem), sorted(unresolved))
    return out


def main():
    mod_dir, layout_path, out_dir = map(Path, sys.argv[1:4])
    layout = json.loads(layout_path.read_text())
    mods = {}
    for name, where in layout.items():
        m = Rel(mod_dir / f"{name}.rel")
        m.place(int(where["image"], 16), int(where["bss"], 16))
        mods[m.id] = m
    out_dir.mkdir(parents=True, exist_ok=True)
    result = {}
    for name, (base, mem, unresolved) in link(mods, 0).items():
        (out_dir / f"{name}.bin").write_bytes(mem)
        m = next(x for x in mods.values() if x.name == name)
        result[name] = {
            "id": m.id, "base": hex(base), "image": hex(m.image), "bss": hex(m.bss), "end": hex(m.end()),
            "prolog": hex(m.sec_addr(m.prolog_sec) + m.prolog) if m.prolog_sec else None,
            "epilog": hex(m.sec_addr(m.epilog_sec) + m.epilog) if m.epilog_sec else None,
            "sections": [{"addr": hex(m.sec_addr(i)), "size": hex(s), "exec": e}
                         for i, (o, e, s) in enumerate(m.secs) if s and i],
            "unresolved_imports": unresolved}
        print(f"{name:28} id {m.id:3} image {m.image:#010x} bss {m.bss:#010x} end {m.end():#010x}"
              + (f"  imports from unplaced modules {unresolved}" if unresolved else ""))
    (out_dir / "rel_layout.json").write_text(json.dumps(result, indent=1))


if __name__ == "__main__":
    main()
