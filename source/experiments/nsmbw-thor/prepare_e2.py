"""Prepare the owner's USA revision 2 binaries and audit the E1 native-runtime migration.

No game bytes are embedded in this file. Outputs belong in ignored build/, never source.
The upstream E1 unpacker and its revision/hash checks remain untouched.
"""
from __future__ import annotations

import argparse
import ast
from collections import Counter
import difflib
import hashlib
import json
from pathlib import Path
import re
import runpy
import struct
import subprocess

PIN = "20f0158dd1d96bd94f821adf80ee361e4fbd414e"
E2_DOL_SHA256 = "0373b4dbffd7c9da9d27eb7257a7677877a3e33ceb81818bab1daa93560b9907"
MAP_SHA256 = "0d0a086ea24a77d3a5850f593009cc40cfb8932c37949128bdac0697a26e05cb"
# Hash after universal-newline normalization, matching read_text() on every OS.
PRELINK_SHA256 = "d3d356adbfa10f3a04e65963acfa4e396a5cba20d6ea7948d9761730ab362419"
E1_BASES = {1: 0x807684C0, 2: 0x8076D680, 3: 0x809A2CC0, 4: 0x80B1C940}
NAMES = {1: "d_profileNP", 2: "d_basesNP", 3: "d_enemiesNP", 4: "d_en_bossNP"}
E2_REL_SHA256 = {
    "d_profileNP": "d6b738eb779d0b0b48f0b6d350e8955d51cbbdf0fb46ffdb621d9538c8fd67c7",
    "d_basesNP": "7f0fb0d0f91e4e82e3d01b9aee3546182fce7acfcd142e3f900dea9a5472f128",
    "d_enemiesNP": "0f3af075bb64e86abfbcdb31def285f67557814656afde8aefa1dc3016e23f87",
    "d_en_bossNP": "8c5d5649d45b461061cd6e633909b9bd8bb9b0380f45745de0cf81cf2c5ca277",
}

# Reviewed correspondence sites. Do not map arbitrary address-shaped numbers:
# gx_dl.cpp's 0x80808081 is a division constant, and MKW remnants are unrelated.
REVIEWED = {
    "runtime/src/guest_profiler.cpp": {0x807684C0, 0x80B8E340},
    "runtime/src/hle/vi.cpp": {0x807684C0, 0x8076D460, 0x8076D680, 0x80990820,
                                  0x809A2CC0, 0x80B11440, 0x80B1C940, 0x80B89AC0},
    "runtime/src/netplay/game_menus.cpp": {0x809953C0, 0x80995400, 0x80995440, 0x80995480,
                                           0x809954C0, 0x80995500, 0x80995540, 0x80995580, 0x80992050},
}
REVISION_HEADER = '''#pragma once

#include <cstdint>

// E1 is the upstream default. Thor E2 must opt in at compile time and translate
// the matching owner-supplied E2 DOL/REL inputs with recomp-e2.yml.
#if defined(NSMBW_DISC_REVISION_E2) && NSMBW_DISC_REVISION_E2
inline constexpr uint32_t kNsmbwDiscRevision = 2;
inline constexpr uint32_t NsmbwRevisionAddress(uint32_t e1, uint32_t e2) { return e2; }
#else
inline constexpr uint32_t kNsmbwDiscRevision = 1;
inline constexpr uint32_t NsmbwRevisionAddress(uint32_t e1, uint32_t e2) { return e1; }
#endif
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


class AddressMap:
    """Strict direct E1 -> E2 mapping; missing ranges never imply identity."""
    def __init__(self, text):
        self.ranges = []
        active = False
        for raw in text.splitlines():
            line = raw.split("#", 1)[0].strip()
            if line.startswith("["):
                active = line == "[E2]"
            elif active and line.startswith("extend"):
                require(line == "extend E1", "E2 must directly extend E1")
            elif active and line:
                m = re.fullmatch(r"([\da-fA-F]+)-([\da-fA-F]+|\*)\s*:\s*([+-])0x([\da-fA-F]+)", line)
                if not m:
                    raise ValueError(f"Unsupported address-map syntax: {line}")
                a, b, sign, d = m.groups()
                self.ranges.append((int(a, 16), 0xFFFFFFFF if b == "*" else int(b, 16), int(d, 16) * (1 if sign == "+" else -1)))
        require(bool(self.ranges), "No E2 address ranges found")

    def map(self, addr):
        found = [addr + d for a, b, d in self.ranges if a <= addr <= b]
        if len(found) != 1:
            raise ValueError(f"No unique E1 -> E2 map for 0x{addr:08X}")
        return found[0]


def patched_prelink(source, game, bases):
    """Execute the pinned upstream REL linker with explicit E2 layout inputs."""
    require(sha(source.encode()) == PRELINK_SHA256, "Upstream prelink tool changed; review required")
    tree = ast.parse(source)
    replaced = set()
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name == "GAME":
                node.value = ast.Call(func=ast.Name(id="Path", ctx=ast.Load()), args=[ast.Constant(str(game))], keywords=[])
                replaced.add(name)
            elif name == "IMAGE_BASE":
                node.value = ast.parse(repr(bases), mode="eval").body
                replaced.add(name)
    require(replaced == {"GAME", "IMAGE_BASE"}, "Upstream linker input structure changed")
    ast.fix_missing_locations(tree)
    exec(compile(tree, "<pinned-prelink-E2-inputs>", "exec"), {"__name__": "__e2_prep__"})


def flat_image(game):
    base = 0x80000000
    flat = bytearray(0x01800000)
    ranges = []
    dol = (game / "main.dol").read_bytes()
    offs = struct.unpack_from(">18I", dol, 0)
    addrs = struct.unpack_from(">18I", dol, 0x48)
    sizes = struct.unpack_from(">18I", dol, 0x90)
    for i, (o, a, n) in enumerate(zip(offs, addrs, sizes)):
        if not n:
            continue
        require(base <= a < a + n <= base + len(flat), "DOL section outside MEM1")
        require(o + n <= len(dol), "Truncated DOL section")
        flat[a-base:a-base+n] = dol[o:o+n]
        ranges.append(("exec" if i < 7 else "data", a, a+n, f"dol_{i}"))
    bss, bss_size = struct.unpack_from(">II", dol, 0xD8)
    ranges.append(("bss", bss, bss+bss_size, "dol_bss"))
    layout = json.loads((game / "nsmbw_rel_layout.json").read_text())
    rel = (game / "nsmbw_linked.rel").read_bytes()
    rb = int(layout["image_start"], 16)
    flat[rb-base:rb-base+len(rel)] = rel
    for m in layout["modules"]:
        for i, sec in m["sections"].items():
            a, n = int(sec["addr"], 16), int(sec["size"], 16)
            kind = "exec" if sec["exec"] else "bss" if a == int(m["bss"], 16) else "data"
            ranges.append((kind, a, a+n, f"{m['name']}_{i}"))
    (game / "nsmbw_flat.bin").write_bytes(flat)
    (game / "ranges.txt").write_text("".join(f"{k} {a:08X} {b:08X} {n}\n" for k,a,b,n in sorted(ranges, key=lambda x:x[1])))
    return flat, ranges, layout


def build_map(repo, out, mapper, flat, ranges, layout):
    """Map upstream named seeds, then discover calls and data/code pointers from E2 itself."""
    ex = [(a,b) for k,a,b,_ in ranges if k == "exec"]
    def executable(a):
        return a % 4 == 0 and any(s <= a < e for s,e in ex)
    def word(a):
        return struct.unpack_from(">I", flat, a-0x80000000)[0]
    names, dropped = {}, []
    for line in (repo / "projects/nsmbw/MAP.txt").read_text().splitlines():
        a, name = line.split(None, 1)
        old = int(a, 16)
        try:
            new = mapper.map(old)
        except ValueError:
            dropped.append({"address":f"0x{old:08X}", "name":name, "reason":"unmapped E1 symbol"})
            continue
        if executable(new):
            names.setdefault(new, name)
        else:
            dropped.append({"address":f"0x{old:08X}", "name":name, "reason":"mapped outside E2 executable ranges"})
    seeds = {0x80004050}
    for m in layout["modules"]:
        seeds.update(int(m[key],16) for key in ("prolog","epilog","unresolved"))
    for kind,a,b,_ in ranges:
        for p in range((a+3)&~3,b-3,4):
            w = word(p)
            if kind == "exec":
                if w >> 26 == 18 and w & 3 == 1:
                    disp = w & 0x03FFFFFC
                    if disp & 0x02000000: disp -= 0x04000000
                    target = (p+disp)&0xFFFFFFFF
                    if target != p+4 and executable(target): seeds.add(target)
                # lis rN,hi followed by addi/ori rM,rN,lo materializes callback pointers.
                if p+8 <= b and w >> 26 == 15 and ((w >> 16)&31) == 0:
                    reg, hi, w2 = (w>>21)&31, (w&0xFFFF)<<16, word(p+4)
                    target = None
                    if w2>>26 == 14 and ((w2>>16)&31) == reg:
                        lo=w2&0xFFFF; target=(hi+(lo if lo<0x8000 else lo-0x10000))&0xFFFFFFFF
                    elif w2>>26 == 24 and ((w2>>21)&31) == reg:
                        target=hi|(w2&0xFFFF)
                    if target is not None and executable(target): seeds.add(target)
            elif kind == "data" and executable(w):
                seeds.add(w)
    seeds.update(names)
    for k,a,_,_ in ranges:
        if k == "exec": seeds.add(a)
    target = out / "MAP-e2.txt"
    target.write_text("".join(f"{a:08x} {names.get(a, f'func_{a:08x}')}\n" for a in sorted(seeds)))
    return {"function_seeds":len(seeds),"mapped_names":len(names),"dropped_e1_seeds":dropped,"map_sha256":sha(target.read_bytes())}


CPP_TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|R"([^ ()\\\t\r\n]*)\([\s\S]*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\b0[xX][\da-fA-F]+[uUlL]*\b|\b[A-Za-z_]\w*\b|\b[\da-fA-F]{8}\b')


def audit_sources(repo, out, mapper, ranges):
    changes, unknown, patch, excluded = [], [], [], []
    unchanged = 0
    for folder in ("runtime/src", "runtime/include", "aurora-main/lib", "aurora-main/include"):
        for path in sorted((repo / folder).rglob("*")):
            if path.suffix not in {".cpp",".h",".hpp",".inc"}: continue
            name=path.relative_to(repo).as_posix()
            if name == "runtime/include/nsmbw_disc_revision.h": continue
            source = (subprocess.check_output(["git","-C",str(repo),"show",f"{PIN}:{name}"],text=True)
                      if name in REVIEWED or name == "runtime/src/system_bridge.cpp" else path.read_text(encoding="utf-8"))
            edits = []
            for m in CPP_TOKEN.finditer(source):
                token=m.group()
                if token.startswith(("//","/*",'"',"'",'R"')): continue
                # Guest addresses occur as integer literals, macro arguments, and func_* names.
                if re.fullmatch(r"0[xX]80[\da-fA-F]{6}[uUlL]*", token):
                    match=re.match(r"0[xX]([\da-fA-F]{8})(.*)",token); old=int(match[1],16)
                    style="literal"
                elif re.fullmatch(r"80[\da-fA-F]{6}",token):
                    old=int(token,16); style="macro"
                elif re.fullmatch(r"func_80[\da-fA-F]{6}",token):
                    old=int(token[5:],16); style="dispatch-symbol"
                else: continue
                if not 0x80004000 <= old < 0x81800000: continue
                line=source.count("\n",0,m.start())+1
                context=source.splitlines()[line-1].strip()
                try: new=mapper.map(old)
                except ValueError:
                    unknown.append({"file":path.relative_to(repo).as_posix(),"line":line,"token":token,"code":context,"reason":"unmapped"}); continue
                if old==new: unchanged+=1; continue
                kinds=[k for k,a,b,_ in ranges if a<=new<b]
                record={"file":path.relative_to(repo).as_posix(),"line":line,"old":f"0x{old:08X}","new":f"0x{new:08X}","token":token,"kind":style,"e2_ranges":kinds,"code":context,"source_sha256":sha(source.encode())}
                if style=="literal": replacement=token[:2]+(f"{new:08x}" if token[2:10].islower() else f"{new:08X}")+token[10:]
                elif style=="macro": replacement=f"{new:08x}" if token.islower() else f"{new:08X}"
                else: replacement="func_"+(f"{new:08x}" if token[5:].islower() else f"{new:08X}")
                record["replacement"]=replacement
                if old not in REVIEWED.get(name,set()):
                    record["decision"] = "excluded_from_migration"
                    record["reason"] = ("integer division magic, not an address" if name.endswith("gx_dl.cpp")
                                        else "disabled or unused MKW-specific remnant; not an NSMBW E1 address")
                    excluded.append(record)
                    continue
                changes.append(record); edits.append((m.start(),m.end(),replacement))
            result=source
            for a,b,replacement in reversed(edits): result=result[:a]+replacement+result[b:]
            if result!=source:
                name=path.relative_to(repo).as_posix()
                patch.extend(difflib.unified_diff(source.splitlines(True),result.splitlines(True),fromfile="a/"+name,tofile="b/"+name))
    (out/"runtime-e2-candidates.patch").write_text("".join(patch))
    (out/"runtime-e2-audit.json").write_text(json.dumps({"changes":changes,"excluded":excluded,"unmapped":unknown,"unchanged_address_tokens":unchanged},indent=2)+"\n")
    return {"reviewed_address_tokens":len(changes),"reviewed_files":len({r['file'] for r in changes}),"excluded_nonaddresses_or_mkw_remnants":len(excluded),"unmapped_tokens":len(unknown),"unchanged_address_tokens":unchanged}


def audit_native_bindings(repo, out, mapper):
    entries=[]
    for path in sorted((repo/"runtime/src").rglob("*.cpp")):
        source=path.read_text()
        # Keep line boundaries while masking comments and quoted strings.
        def mask(m):
            value=m.group()
            return re.sub(r"[^\n]"," ",value) if value.startswith(("//","/*",'"',"'",'R"')) else value
        code=CPP_TOKEN.sub(mask,source)
        for match in re.finditer(r"\b(?:PPC_NATIVE_OVERRIDE(?:_VOID)?|REGISTER_NATIVE_FUNCTION(?:_AS)?)\s*\(\s*(?:0x)?([\da-fA-F]{8})\s*,",code):
            addr=int(match[1],16)
            if addr<0x80004000: continue
            new=mapper.map(addr)
            require(new==addr,f"Native binding changed and needs body-level review: {path} {addr:08X}")
            entries.append({"file":path.relative_to(repo).as_posix(),"line":code.count("\n",0,match.start())+1,"address":f"0x{addr:08X}"})
    (out/"native-bindings-e2.json").write_text(json.dumps({"unchanged_lexical_entries":len(entries),"unique_addresses":len({r['address'] for r in entries}),"includes_preprocessor_disabled_entries":True,"changed_addresses":[],"entries":entries},indent=2)+"\n")
    return {"unchanged_lexical_entries":len(entries),"changed_addresses":0}


def apply_reviewed_runtime(repo, mapper):
    """Small, enumerated, idempotent migration preserving the upstream E1 build."""
    applied=[]; source_patch=[]
    for name, addresses in REVIEWED.items():
        path=repo/name
        original=subprocess.check_output(["git","-C",str(repo),"show",f"{PIN}:{name}"],text=True)
        result=original
        seen=Counter()
        def replace(m):
            token=m.group(); addr=int(token[2:10],16)
            if addr not in addresses: return token
            seen[addr]+=1
            return f"NsmbwRevisionAddress({token}, 0x{mapper.map(addr):08X}u)"
        result=re.sub(r"\b0x80[\da-fA-F]{6}[uUlL]*\b",replace,result)
        require(set(seen)==addresses and all(n==1 for n in seen.values()),f"Reviewed site count changed: {name} {seen}")
        result='#include "nsmbw_disc_revision.h"\n'+result
        current=path.read_text()
        snippets=[f"NsmbwRevisionAddress(0x{addr:08X}u, 0x{mapper.map(addr):08X}u)" for addr in addresses]
        already_applied=all(current.count(s)==1 for s in snippets) and current.count('#include "nsmbw_disc_revision.h"')==1
        require(current==original or already_applied, f"Refusing to overwrite unrelated edits before migration: {name}")
        if not already_applied:
            path.write_text(result,encoding="utf-8",newline="\n")
        source_patch.extend(difflib.unified_diff(original.splitlines(True),result.splitlines(True),fromfile="a/"+name,tofile="b/"+name))
        applied.append({"file":name,"before_sha256":sha(original.encode()),"after_sha256":sha(path.read_text().encode()),"reviewed_sites":len(addresses),"preserved_additional_edits":already_applied and current!=result})
    name="runtime/src/system_bridge.cpp"; path=repo/name
    original=subprocess.check_output(["git","-C",str(repo),"show",f"{PIN}:{name}"],text=True)
    old='entries.push_back({0x80000004u, 0x30310001u, "Disc maker/id"});'
    new='entries.push_back({0x80000004u, 0x30310000u | kNsmbwDiscRevision, "Disc maker/id"});'
    require(original.count(old)==1,"Disc seed source site changed")
    result='#include "nsmbw_disc_revision.h"\n'+original.replace(old,new)
    result=result.replace('// "01", disc 0, version 1 (boot.bin)','// "01", disc 0, selected revision (boot.bin)')
    current=path.read_text()
    already_applied=current.count(new)==1 and current.count('#include "nsmbw_disc_revision.h"')==1
    require(current==original or already_applied,f"Refusing to overwrite unrelated edits before migration: {name}")
    if not already_applied: path.write_text(result,encoding="utf-8",newline="\n")
    header=repo/"runtime/include/nsmbw_disc_revision.h"
    require(not header.exists() or header.read_text()==REVISION_HEADER,"Unrelated revision header exists")
    header.write_text(REVISION_HEADER,encoding="utf-8",newline="\n")
    applied.append({"file":name,"before_sha256":sha(original.encode()),"after_sha256":sha(path.read_text().encode()),"reviewed_sites":1,"preserved_additional_edits":already_applied and current!=result})
    source_patch.extend(difflib.unified_diff(original.splitlines(True),result.splitlines(True),fromfile="a/"+name,tofile="b/"+name))
    source_patch.extend(difflib.unified_diff([],REVISION_HEADER.splitlines(True),fromfile="/dev/null",tofile="b/runtime/include/nsmbw_disc_revision.h"))
    return applied,"".join(source_patch)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo",type=Path,required=True)
    parser.add_argument("--disc",type=Path,required=True)
    parser.add_argument("--address-map",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--apply-reviewed-runtime",action="store_true",help="Apply only the 19 enumerated address sites plus revision seed, with E1 defaults preserved")
    args=parser.parse_args()
    repo=args.repo.resolve(); disc=args.disc.resolve(); out=args.output.resolve()
    require(subprocess.check_output(["git","-C",str(repo),"rev-parse","HEAD"],text=True).strip()==PIN,"Unsupported source commit")
    require("build" in out.parts,"Private outputs must be inside ignored build/")
    map_bytes=args.address_map.read_bytes(); require(sha(map_bytes)==MAP_SHA256,"Address map changed; review required")
    mapper=AddressMap(map_bytes.decode())
    boot=(disc/"sys/boot.bin").read_bytes(); require(boot[:8]==b"SMNE01\x00\x02","Expected owner's SMNE01 revision 2")
    dol=(disc/"sys/main.dol").read_bytes(); require(sha(dol)==E2_DOL_SHA256,"E2 DOL hash mismatch")
    game=out/"game"; game.mkdir(parents=True,exist_ok=True)
    (game/"main.dol").write_bytes(dol)
    lz11=runpy.run_path(str(repo/"projects/nsmbw/tools/unpack_game.py"))["lz11"]
    hashes={"main.dol":sha(dol)}
    for name in NAMES.values():
        rel=lz11((disc/"files/rels"/(name+".rel.LZ")).read_bytes())
        require(sha(rel)==E2_REL_SHA256[name],f"E2 REL hash mismatch: {name}")
        (game/(name+".rel")).write_bytes(rel); hashes[name+".rel"]=sha(rel)
    bases={mid:mapper.map(addr) for mid,addr in E1_BASES.items()}
    patched_prelink((repo/"projects/nsmbw/tools/prelink_rels.py").read_text(),game,bases)
    flat,ranges,layout=flat_image(game)
    map_report=build_map(repo,out,mapper,flat,ranges,layout)
    source_report=audit_sources(repo,out,mapper,ranges)
    native_report=audit_native_bindings(repo,out,mapper)
    applied=[]
    if args.apply_reviewed_runtime:
        applied,source_patch=apply_reviewed_runtime(repo,mapper)
        (out/"runtime-e2-opt-in.patch").write_text(source_patch)
    (out/"runtime-e2-applied.json").write_text(json.dumps({"opt_in_macro":"NSMBW_DISC_REVISION_E2=1","applied":applied},indent=2)+"\n")
    # Separate manifest: the original E1 identity and hash are never weakened.
    manifest=(repo/"projects/nsmbw/recomp.yml").read_text()
    manifest=manifest.replace("North America rev 1 (SMNE01 v1)","North America rev 2 (SMNE01 v2)")
    manifest=manifest.replace("retargeted to NSMBW SMNE01 rev 1 (branch nsmbw)","retargeted to E2 only when compiled with NSMBW_DISC_REVISION_E2=1")
    manifest=manifest.replace("New Super Mario Bros. Wii (USA rev 1)","New Super Mario Bros. Wii (USA rev 2)")
    manifest=manifest.replace("id: nsmbw-smne01","id: nsmbw-smne01-e2")
    manifest=manifest.replace("../game/main.dol",(game/"main.dol").as_posix())
    manifest=manifest.replace("../game/nsmbw_linked.rel",(game/"nsmbw_linked.rel").as_posix())
    manifest=manifest.replace("6d755ec24c395fee600a856a85785e0e99c0b7ecb680d573a6cdd1aa6f7beee7",E2_DOL_SHA256)
    manifest=manifest.replace("load_address: 0x807684C0",f"load_address: 0x{bases[1]:08X}")
    manifest=manifest.replace("path: projects/nsmbw/MAP.txt",f"path: {(out/'MAP-e2.txt').as_posix()}")
    manifest=re.sub(r"^workspace_root:.*$",f"workspace_root: {repo.as_posix()}",manifest,flags=re.M)
    (out/"recomp-e2.yml").write_text(manifest)
    report={"status":"prepared_and_audited_not_built", "source_commit":PIN,"disc_id":"SMNE01","revision":2,"input_sha256":hashes,"address_map_sha256":sha(map_bytes),"rel_image_bases":{str(k):f"0x{v:08X}" for k,v in bases.items()},"map":map_report,"runtime":source_report,"native_bindings":native_report,"runtime_patch_applied":bool(applied),"limits":["Native runtime has NOT been built or validated in game.","Address correspondence does not prove a replaced guest function has identical behavior.","E2 function discovery supplements named seeds; it is not an exhaustive function-boundary proof.","No build, boot, input, performance, save or gameplay validation is implied."]}
    (out/"preparation.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))


if __name__=="__main__": main()
