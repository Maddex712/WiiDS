# Match NSMBW DOL functions to SPM DOL functions by opcode skeleton + call-graph propagation.
import struct,sys,os,re,collections
sys.path.insert(0,os.path.expanduser("~/work")); from dolread import Dol
H=os.path.expanduser("~/work")
nd=Dol(os.path.expanduser("~/mnt/Recomps/NewSuperMarioBrosWii/Disc/DATA/sys/main.dol"))
sd=Dol(H+"/game/main.dol")
def load(path,dol):
    fs=[]
    for l in open(path):
        p=l.split()
        if len(p)>=1:
            try: a=int(p[0],16)
            except: continue
            fs.append((a,p[1] if len(p)>1 else ""))
    fs.sort(); tx=[(a,a+len(b)) for a,b in dol.text()]
    inr=lambda a:any(s<=a<e for s,e in tx)
    fs=[f for f in fs if inr(f[0])]
    out={}
    for i,(a,n) in enumerate(fs):
        e=fs[i+1][0] if i+1<len(fs) else a+4
        e=min(e,[te for ts,te in tx if ts<=a<te][0]); out[a]=(n,e-a)
    return out
nf=load(H+"/nsmbw-src/projects/nsmbw/MAP.txt",nd); sf=load(H+"/spm-wc/projects/spm/MAP.txt",sd)
def skel(dol,a,sz):
    w=struct.unpack(">%dI"%(sz//4),dol.read(a,sz)); r=[];calls=[]
    for i,x in enumerate(w):
        op=x>>26
        if op in(31,19,59,63,4): k=(op,(x>>1)&0x3FF)
        elif op==18:
            k=(op,x&1)
            if x&1:
                t=(a+4*i+((x&0x3FFFFFC)-(0x4000000 if x&0x2000000 else 0)))&0xFFFFFFFF if not x&2 else x&0x3FFFFFC
                calls.append(t)
        else: k=(op,)
        r.append(k)
    return tuple(r),calls
N={a:skel(nd,a,s) for a,(n,s) in nf.items() if s>=4}
S={a:skel(sd,a,s) for a,(n,s) in sf.items() if s>=4}
pair={}  # nsmbw->spm
# seed by name
sn={n:a for a,(n,s) in sf.items() if n and not re.match(r"fn_|zz_|lbl_",n)}
for a,(n,s) in nf.items():
    if n in sn and a in N and sn[n] in S: pair[a]=sn[n]
print("name seeds",len(pair))
def add(a,b):
    if a in pair or b in pairr: return False
    pair[a]=b; pairr[b]=a; return True
pairr={b:a for a,b in pair.items()}
# exact skeleton unique
d1=collections.defaultdict(list); d2=collections.defaultdict(list)
for a,(k,c) in N.items():
    if len(k)>=12: d1[k].append(a)
for a,(k,c) in S.items():
    if len(k)>=12: d2[k].append(a)
n0=len(pair)
for k,l in d1.items():
    if len(l)==1 and len(d2.get(k,[]))==1: add(l[0],d2[k][0])
print("skeleton unique",len(pair)-n0)
# call-graph propagation
for it in range(8):
    ch=0
    for a,b in list(pair.items()):
        if a not in N or b not in S: continue
        ca,cb=N[a][1],S[b][1]
        segs=[]
        if len(ca)==len(cb): segs=list(zip(ca,cb))
        else:
            # anchor on already-paired callees; pair unmatched runs of equal length between anchors
            ia=[(i,pair[x]) for i,x in enumerate(ca) if x in pair]
            ib={y:i for i,y in enumerate(cb)}
            an=[(i,ib[y]) for i,y in ia if y in ib]
            an=[(-1,-1)]+an+[(len(ca),len(cb))]
            for (i0,j0),(i1,j1) in zip(an,an[1:]):
                if i1-i0==j1-j0 and i1-i0>1: segs+=list(zip(ca[i0+1:i1],cb[j0+1:j1]))
        if segs:
            for x,y in segs:
                if x in N and y in S and len(N[x][0])>=4 and abs(len(N[x][0])-len(S[y][0]))<=max(8,len(N[x][0])//4):
                    ch+=add(x,y)
    print("iter",it,ch)
    if not ch:break
json=__import__("json")
out={"%08X"%a:"%08X"%b for a,b in pair.items()}
json.dump(out,open(H+"/spm-wc/projects/spm/bindiff.json","w"))
print("total",len(pair),"of",len(nf))
