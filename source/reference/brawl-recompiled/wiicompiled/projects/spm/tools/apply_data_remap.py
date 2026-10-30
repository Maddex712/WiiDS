# Rewrite resolved SDK data addresses in runtime sources. Usage: apply_data_remap.py <repo root>
import re,sys,csv,os
sys.path.insert(0,os.path.dirname(__file__)); from data_resolve import resolve
root=sys.argv[1]; rt=os.path.join(root,"runtime")
SKIP=("cryptopp","pugixml","recomp_mod_loader","netplay","display_settings","dynamic_aspect","gx_egg","strap_scene","game_layout","egg_decomp","music_attenuation","guest_profiler","gx_fatal")
rows=list(csv.DictReader(open(os.path.join(root,"projects/spm/runtime_remap.csv"))))
cand={int(r["nsmbw_addr"],16) for r in rows if r["status"]=="not-a-function-start"}
pat=re.compile(r"\b(0x)([0-9A-Fa-f]{8})(u?)\b")
log=[]
for dp,_,fs in os.walk(rt):
    for f in fs:
        if not f.endswith((".cpp",".h",".hpp",".inc")) or any(s in os.path.join(dp,f) for s in SKIP): continue
        p=os.path.join(dp,f); t=open(p,encoding="utf-8",errors="surrogateescape").read(); ch=[]
        def sub(m):
            a=int(m.group(2),16)
            if a not in cand or a<0x80300000: return m.group(0)
            v,how=resolve(a)
            if v is None: ch.append((a,None,"none")); return m.group(0)
            ch.append((a,v,how)); return m.group(1)+("%08X"%v if m.group(2).isupper() or not m.group(2).islower() else "%08x"%v)+m.group(3)
        n=pat.sub(sub,t)
        for a,v,how in ch: log.append((os.path.relpath(p,rt),"%08X"%a,"%08X"%v if v else "",how))
        if n!=t: open(p,"w",encoding="utf-8",errors="surrogateescape").write(n)
csv.writer(open(os.path.join(root,"projects/spm/data_actions.csv"),"w",newline="")).writerows([("file","nsmbw","spm","how")]+log)
print(len(log),"refs;",sum(1 for l in log if l[2]),"rewritten")
