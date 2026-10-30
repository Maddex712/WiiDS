# Map NSMBW data addresses to SPM by aligning matched function pairs and pairing the addresses they reference.
import struct,sys,os,json,collections,difflib
H=os.path.expanduser("~/work"); sys.path.insert(0,H)
exec(open(H+"/spm-wc/projects/spm/tools/bindiff.py").read().split("pair={}")[0])  # nd,sd,nf,sf,N,S,skel
pair={int(k,16):int(v,16) for k,v in json.load(open(H+"/spm-wc/projects/spm/bindiff.json")).items()}
def refs(dol,a,sz,sda,sda2):
    w=struct.unpack(">%dI"%(sz//4),dol.read(a,sz)); hi={}; out={}
    for i,x in enumerate(w):
        op=x>>26; rd=(x>>21)&31; ra=(x>>16)&31; si=x&0xFFFF; ss=si-0x10000 if si&0x8000 else si
        addr=None
        if op==15:
            if ra==0: hi[rd]=si<<16
            else: hi.pop(rd,None)
        elif op==14:
            if ra==13: addr=sda+ss
            elif ra==2: addr=sda2+ss
            elif ra in hi: addr=hi[ra]+ss
            if addr is None: hi.pop(rd,None)
        elif op==24:  # ori rA,rS,imm : rS=bits21-25, rA=bits16-20
            if rd in hi: addr=hi[rd]|si
        elif 32<=op<=55:
            if ra==13: addr=sda+ss
            elif ra==2: addr=sda2+ss
            elif ra in hi: addr=hi[ra]+ss
            if op<=43 and rd in hi and rd!=ra: hi.pop(rd,None)
        if addr is not None and 0x80300000<=addr<0x81800000: out[i]=addr
    return out
votes=collections.defaultdict(collections.Counter)
for a,b in pair.items():
    if a not in N or b not in S: continue
    ka,kb=N[a][0],S[b][0]
    ra=refs(nd,a,nf[a][1],0x8042F680,0x80433080); rb=refs(sd,b,sf[b][1],0x80574F20,0x80576280)
    if not ra or not rb: continue
    sm=difflib.SequenceMatcher(None,ka,kb,autojunk=False)
    for blk in sm.get_matching_blocks():
        for k in range(blk.size):
            i,j=blk.a+k,blk.b+k
            if i in ra and j in rb: votes[ra[i]][rb[j]]+=1
res={}
for n,c in votes.items():
    (m,v),=c.most_common(1); tot=sum(c.values())
    res[n]=(m,v,tot)
json.dump({"%08X"%k:["%08X"%v[0],v[1],v[2]] for k,v in res.items()},open(H+"/spm-wc/projects/spm/datamap.json","w"))
print("mapped",len(res))
