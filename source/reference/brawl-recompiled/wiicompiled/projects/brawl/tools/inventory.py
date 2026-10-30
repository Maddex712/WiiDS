"""List every SPM guest address the runtime hard-codes, with the SPM symbol it falls in. -> stdout tsv"""
import bisect,re,sys
from pathlib import Path
wc=Path(sys.argv[1])
m=[l.split(None,1) for l in (wc/"projects/spm/MAP.txt").read_text().splitlines() if l.strip()]
starts=[int(a,16) for a,_ in m]; names=[n.strip() for _,n in m]
lst={}
for l in (wc/"projects/spm/pal/spm.us0.lst").read_text().splitlines():
    if ":" in l and not l.startswith("//"):
        a,n=l.split(":",1)
        if re.fullmatch(r"[0-9A-Fa-f]{8}",a): lst[int(a,16)]=n
LIT=re.compile(r"(?<![0-9A-Za-z_])(0x)([0-9A-Fa-f]{8})(?![0-9A-Fa-f])|(\(\s*)(80[0-9A-Fa-f]{6})(?![0-9A-Za-z_])")
SDA=re.compile(r"(r13 - 0x|Sda1Offset = 0x|OffsetFromR13 = 0x)([0-9A-Fa-f]{4})")
for p in sorted((wc/"runtime").rglob("*")):
    if p.suffix not in (".cpp",".h",".inc") or "third_party" in p.parts: continue
    for i,line in enumerate(p.read_text(encoding="utf-8",errors="replace").split("\n")):
        for mm in LIT.finditer(line):
            a=int(mm.group(2) or mm.group(4),16)
            if not 0x80000000<=a<0x81800000: continue
            k=bisect.bisect_right(starts,a)-1
            nm=lst.get(a) or (names[k]+("+%X"%(a-starts[k]) if a!=starts[k] else "") if k>=0 and a<0x80600000 else "")
            print(f"{p.relative_to(wc)}\t{i+1}\t{a:08X}\t{nm}\t{line.strip()[:100]}")
        for mm in SDA.finditer(line):
            print(f"{p.relative_to(wc)}\t{i+1}\tSDA-{mm.group(2)}\t\t{line.strip()[:100]}")
