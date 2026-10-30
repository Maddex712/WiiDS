"""Offline OSLink for Super Paper Mario (R8PE01 rev 0): relF.bin (LZ10) -> one pre-linked REL at its boot heap address.
relmgr.c allocates the decompressed REL from HEAP_MAIN, then its BSS as the next block, then OSLink + prolog.
Usage: prelink_spm_rel.py <files/rel/relF.bin> <out.rel> <out.json> [image_base] [bss_base]"""
import json,struct,sys
src,out_rel,out_json=sys.argv[1:4]
PROLOG_SEEN=0x80C34900   # prolog address the runtime reported at boot (relMain -> prolog)
def lz10(b):
    assert b[0]==0x10; n=b[1]|b[2]<<8|b[3]<<16; o=bytearray(); i=4
    while len(o)<n:
        f=b[i]; i+=1
        for k in range(8):
            if len(o)>=n: break
            if f&(0x80>>k):
                d=b[i]<<8|b[i+1]; i+=2; ln=(d>>12)+3; disp=(d&0xFFF)+1
                for _ in range(ln): o.append(o[-disp])
            else: o.append(b[i]); i+=1
    return bytes(o)
raw=lz10(open(src,"rb").read())
(mid,_n,_p,nsec,secoff,_no,_ns,ver,bss_size,rel_off,imp_off,imp_size,prolog_sec,epilog_sec,unres_sec,bss_sec,prolog,epilog,unres)=struct.unpack(">IIIIIIIIIIIIBBBBIII",raw[:0x40])
secs=[]
for i in range(nsec):
    o,s=struct.unpack(">II",raw[secoff+8*i:secoff+8*i+8]); secs.append((o&~1,bool(o&1),s))
image=int(sys.argv[4],16) if len(sys.argv)>4 else PROLOG_SEEN-(secs[prolog_sec][0]+prolog)
assert image%0x20==0, hex(image)
# BSS: next HEAP_MAIN block after the REL image (0x20-aligned data after a 0x10..0x20 block header).
bss=int(sys.argv[5],16) if len(sys.argv)>5 else (image+len(raw)+0x1F&~0x1F)+0x20
print("module",mid,"ver",ver,"sections",nsec,"bss_size",hex(bss_size),"len",hex(len(raw)),"image",hex(image),"bss",hex(bss))
def sec_addr(i):
    o,_e,s=secs[i]
    return bss if (o==0 and s and i!=0 and not secs[i][1] and i==bss_sec_guess) else image+o
bss_sec_guess=next(i for i,(o,e,s) in enumerate(secs) if o==0 and s and i!=0)
end=bss+bss_size; base=image
mem=bytearray(end-base); mem[0:len(raw)]=raw
def rd32(a): return struct.unpack(">I",mem[a-base:a-base+4])[0]
def wr32(a,v): mem[a-base:a-base+4]=struct.pack(">I",v&0xFFFFFFFF)
def wr16(a,v): mem[a-base:a-base+2]=struct.pack(">H",v&0xFFFF)
cnt={}
for k in range(imp_size//8):
    imp_mod,ro=struct.unpack(">II",raw[imp_off+8*k:imp_off+8*k+8]); cur=0; c=ro
    while True:
        delta,typ,ssec,add=struct.unpack(">HBBI",raw[c:c+8]); c+=8
        if typ==203: break
        if typ==202: cur=sec_addr(ssec); continue
        cur+=delta; dst=cur
        tgt=add if imp_mod==0 else sec_addr(ssec)+add
        assert imp_mod in (0,mid), imp_mod
        cnt[typ]=cnt.get(typ,0)+1
        if typ in(0,201): continue
        if typ==1: wr32(dst,tgt)
        elif typ==2: wr32(dst,(rd32(dst)&~0x03FFFFFC)|(tgt&0x03FFFFFC))
        elif typ in(3,4): wr16(dst,tgt)
        elif typ==5: wr16(dst,tgt>>16)
        elif typ==6: wr16(dst,(tgt>>16)+(1 if tgt&0x8000 else 0))
        elif typ in(7,8,9): wr32(dst,(rd32(dst)&~0xFFFC)|(tgt&0xFFFC))
        elif typ==10: wr32(dst,(rd32(dst)&~0x03FFFFFC)|((tgt-dst)&0x03FFFFFC))
        elif typ in(11,12,13): wr32(dst,(rd32(dst)&~0xFFFC)|((tgt-dst)&0xFFFC))
        else: sys.exit("unhandled reloc %d"%typ)
print("relocs",cnt)
# Synthetic REL (no imports), sections as absolute ranges from base: text sections exec, others data, plus bss.
syn=[(0,False,0)]
for i,(o,e,s) in enumerate(secs):
    if i==0 or not s: continue
    if i==bss_sec_guess: syn.append((bss-base,False,s))
    else: syn.append((o,e,s))
hdr_size=0x4C+8*len(syn); first=min(o for o,e,s in syn if s)
assert hdr_size<=first,(hex(hdr_size),hex(first))
size=len(mem)
hdr=struct.pack(">IIIIIIIIIIIIBBBBIII",0x7E,0,0,len(syn),0x4C,0,0,3,0,size,size,0,0,0,0,0,0,0,0)+struct.pack(">III",0x20,0x20,size)
o=bytearray(mem); o[0:hdr_size]=hdr+b"".join(struct.pack(">II",a|(1 if e else 0),s) for a,e,s in syn)
open(out_rel,"wb").write(o)
lay={"image":hex(image),"bss":hex(bss),"bss_size":hex(bss_size),"end":hex(end),"decompressed_size":hex(len(raw)),
     "prolog":hex(sec_addr(prolog_sec)+prolog),"epilog":hex(sec_addr(epilog_sec)+epilog),"unresolved":hex(sec_addr(unres_sec)+unres),
     "sections":{str(i):{"addr":hex(sec_addr(i)),"size":hex(s),"exec":e} for i,(oo,e,s) in enumerate(secs) if s}}
json.dump(lay,open(out_json,"w"),indent=2); print(json.dumps(lay,indent=1))
