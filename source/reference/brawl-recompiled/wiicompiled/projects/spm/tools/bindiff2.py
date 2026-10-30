# Interval-constrained skeleton matching for runtime hook functions with no SPM symbol.
import os,sys,json,csv,bisect,difflib
H=os.path.expanduser("~/work"); sys.path.insert(0,H)
exec(open(H+"/spm-wc/projects/spm/tools/bindiff.py").read().split("pair={}")[0])
pair={int(k,16):int(v,16) for k,v in json.load(open(H+"/spm-wc/projects/spm/bindiff.json")).items()}
# anchors must be monotonic: keep longest increasing subsequence in spm order
an=sorted(pair.items()); 
import bisect
tails=[];idx=[];prev=[-1]*len(an)
for i,(a,b) in enumerate(an):
    j=bisect.bisect_left(tails,b)
    if j==len(tails): tails.append(b); idx.append(i)
    else: tails[j]=b; idx[j]=i
    prev[i]=idx[j-1] if j else -1
k=idx[-1]; mono=[]
while k!=-1: mono.append(an[k]); k=prev[k]
mono.sort(); A=[a for a,b in mono]; B=[b for a,b in mono]
print("monotonic anchors",len(mono),"of",len(an))
used=set(pair.values()); sfs=sorted(S)
def cands(a):
    i=bisect.bisect_right(A,a)
    lo=B[i-1] if i else 0x80004000; hi=B[i] if i<len(B) else 0x80500000
    return [s for s in sfs if lo<s<hi and s not in used]
rows=list(csv.DictReader(open(H+"/spm-wc/projects/spm/runtime_remap.csv")))
res={}
for r in rows:
    if r["status"]!="name-not-in-spm": continue
    a=int(r["nsmbw_addr"],16)
    if a not in N: print(r["name"],"not a dol function"); continue
    sc=[]
    for s in cands(a):
        ratio=difflib.SequenceMatcher(None,N[a][0],S[s][0],autojunk=False).ratio()
        sc.append((ratio,s))
    sc.sort(reverse=True)
    top=sc[:3]
    print(r["name"][:40],hex(a),nf[a][1],"cands",len(sc),[("%.2f"%x,hex(s),sf[s][1]) for x,s in top])
    if top and top[0][0]>=0.8 and (len(top)<2 or top[0][0]-top[1][0]>=0.1): res["%08X"%a]="%08X"%top[0][1]
json.dump(res,open(H+"/spm-wc/projects/spm/bindiff2.json","w")); print("accepted",len(res))
