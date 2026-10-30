"""Rewrite remaining NSMBW function-address literals (not hook macros: InvokeIndirectCpu, compares, tables) by symbol name.
Only literals still identical to the pristine NSMBW runtime are touched. Usage: apply_literal_remap.py <repo root> <pristine runtime>"""
import re,sys,json,os,csv
from pathlib import Path
from collections import defaultdict
root=Path(sys.argv[1]); orig=Path(sys.argv[2]); rt=root/"runtime"
def load_map(p):
    d={}
    for l in p.read_text().splitlines():
        a=l.split(None,1)
        if len(a)==2 and not a[1].startswith("0x"): d[int(a[0],16)]=a[1].strip()
    return d
nsmbw=load_map(root/"projects/nsmbw/MAP.txt")
spm=defaultdict(set)
for l in (root/"projects/spm/us0_names.tsv").read_text().splitlines():
    us,eu,_s,_e,usn=l.split("\t"); spm[eu].add(int(us,16))
    if not re.match(r"(fn|lbl|zz)_",usn): spm[usn].add(int(us,16))
for l in open(root.parent/"spm-decomp/config/US0/symbols.txt",encoding="utf-8",errors="replace"):
    m=re.match(r"(\S+)\s*=\s*\.(text|init):0x([0-9A-Fa-f]{8});\s*//\s*(.*)",l)
    if m and "type:function" in m.group(4) and not re.match(r"(fn|lbl|zz)_",m.group(1)): spm[m.group(1)].add(int(m.group(3),16))
for n,a in {"WPADInit":0x802CC950,"WPADGetInfoAsync":0x802CE4C4,"WPADControlMotor":0x802CE684,"DCStoreRange":0x8026D830}.items(): spm[n].add(a)
for k,v in json.loads((root/"projects/spm/bindiff.json").read_text()).items():
    nm=nsmbw.get(int(k,16))
    if nm and not spm.get(nm): spm[nm].add(int(v,16))
pat=re.compile(r"(?<![0-9A-Za-z_])((?:0x)?)([0-9A-Fa-f]{8})(?![0-9A-Fa-f])")
log=[]
for p in sorted(list((rt/"src").rglob("*"))+list((rt/"include").rglob("*"))):
    if p.suffix not in(".cpp",".h",".hpp",".inc") or any(s in str(p) for s in("third_party","netplay","mod_loader","dynamic_aspect","display_settings","gx_egg","gx_fatal")): continue
    rel=p.relative_to(rt); op=orig/rel
    if not op.exists(): continue
    new=p.read_text(errors="surrogateescape").split("\n"); old=op.read_text(errors="surrogateescape").split("\n")
    # compare line by line only where line counts match the pristine file region; else use text-identity of the line
    oldset=set(old); ch=False
    for i,l in enumerate(new):
        if l not in oldset or l.lstrip().startswith("//"): continue
        def sub(m):
            a=int(m.group(2),16)
            if not(0x80004000<=a<0x80340000) or a not in nsmbw: return m.group(0)
            c=spm.get(nsmbw[a])
            if not c or len(c)!=1: log.append((str(rel),i+1,"%08X"%a,nsmbw[a],"unresolved" if not c else "ambiguous")); return m.group(0)
            v=next(iter(c)); log.append((str(rel),i+1,"%08X"%a,nsmbw[a],"%08X"%v))
            return m.group(1)+("%08X"%v if m.group(2).isupper() or not any(ch.isalpha() for ch in m.group(2)) else "%08x"%v)
        n=pat.sub(sub,l)
        if n!=l: new[i]=n; ch=True
    if ch: p.write_text("\n".join(new),errors="surrogateescape")
csv.writer(open(root/"projects/spm/literal_actions.csv","w",newline="")).writerows([("file","line","nsmbw","name","spm")]+log)
print(sum(1 for l in log if l[4] not in("unresolved","ambiguous")),"rewritten;",sum(1 for l in log if l[4]=="unresolved"),"unresolved")
