# Resolve NSMBW data addresses to SPM using datamap.json (piecewise-constant deltas). Prints/saves resolutions.
import json,bisect,csv,os,sys
P=os.path.expanduser("~/work/spm-wc/projects/spm/")
m={int(k,16):int(v[0],16) for k,v in json.load(open(P+"datamap.json")).items()}
ks=sorted(m)
def resolve(a):
    if a in m: return m[a],"exact"
    i=bisect.bisect_left(ks,a); lo=ks[i-1] if i else None; hi=ks[i] if i<len(ks) else None
    if lo is not None and hi is not None:
        dl,dh=m[lo]-lo,m[hi]-hi
        if dl==dh: return a+dl,"interp(%d..%d)"%(a-lo,hi-a)
    best=None
    for n in (lo,hi):
        if n is not None and (best is None or abs(n-a)<abs(best-a)): best=n
    if best is not None and abs(best-a)<=0x80: return a+m[best]-best,"near(%d)"%abs(best-a)
    return None,"none"
if __name__=="__main__":
    rows=list(csv.DictReader(open(P+"runtime_remap.csv")))
    out={}
    for r in rows:
        if r["status"]!="not-a-function-start": continue
        fl=r["files"]
        if any(x in fl for x in ("cryptopp","pugixml","recomp_mod_loader","netplay")): continue
        a=int(r["nsmbw_addr"],16)
        if a<0x80300000: continue
        v,how=resolve(a); out[r["nsmbw_addr"]]=(v,how,fl)
    for k,(v,how,fl) in sorted(out.items()): print(k,"->",("0x%08X"%v) if v else None,how,fl)
