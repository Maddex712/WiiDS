"""Import the creator's exact Rebalanced 2.1 patch into a PRIVATE disc copy.

No Wii instructions are executed or written to ARM64. XML's data patches become
portable named tables. Its code patches are implemented by port/kaizo.cpp and
scoped source adaptations. The ZIP and the player's disc are never bundled here.
"""
from pathlib import Path, PurePosixPath
import hashlib, json, os, shutil, struct, zipfile, xml.etree.ElementTree as ET

PATCH_SHA256='b0b43ee39e3b2c2b7a3dcc4bc33d77718cb97da4b95f490146ec8a80ceddd548'
DOL_SHA256='2c680585a8f58e1cc9c5521b579057f12b124ff0ef409e470a57606c50a93c09'
PREFIX='KaizoMarioGalaxyRebalancedV2.1/'
HEADER='KMGR21|'+PATCH_SHA256

def sha(data): return hashlib.sha256(data).hexdigest()

def rebuild_fst(files):
    """Host-backed DVD catalogue; original disc offsets are no longer used."""
    entries=[];strings=bytearray(b'\0')
    def walk(folder,parent,name):
        index=len(entries);name_offset=0
        if name:
            name_offset=len(strings);strings.extend(name.encode('shift_jis')+b'\0')
        entries.append([0x01000000|name_offset,parent,0])
        children=sorted(folder.iterdir(),key=lambda p:p.name.casefold())
        if len({p.name.casefold() for p in children})!=len(children):raise ValueError('Case-colliding mod paths')
        for child in children:
            if child.is_symlink():raise ValueError('Linked input in mod files')
            if child.is_dir():walk(child,index,child.name)
            else:
                off=len(strings);strings.extend(child.name.encode('shift_jis')+b'\0')
                entries.append([off,0,child.stat().st_size])
        entries[index][2]=len(entries)
    walk(Path(files),0,'')
    return b''.join(struct.pack('>III',*entry) for entry in entries)+strings

def apply(root,patch):
    root=Path(root).resolve();patch=Path(patch)
    data=patch.read_bytes()
    if sha(data)!=PATCH_SHA256:raise ValueError('Choose the official Kaizo Rebalanced 2.1 ZIP; this patch is a different version')
    boot=(root/'sys/boot.bin').read_bytes()
    if boot[:6]!=b'RMGE01' or boot[6:8]!=b'\0\0':raise ValueError('Kaizo requires clean USA RMGE01 revision 0')
    dol=(root/'sys/main.dol').read_bytes()
    if sha(dol)!=DOL_SHA256:raise ValueError('Kaizo source executable differs from the supported clean USA disc')
    def offset(address,length=1):
        for off,count,adr,siz in [(0,7,0x48,0x90),(0x1c,11,0x64,0xac)]:
            for i in range(count):
                source=struct.unpack_from('>I',dol,off+i*4)[0]
                start=struct.unpack_from('>I',dol,adr+i*4)[0]
                size=struct.unpack_from('>I',dol,siz+i*4)[0]
                if start<=address and address+length<=start+size:return source+address-start
        raise ValueError('Patch pointer is outside the supported executable')
    def string(address):
        if not address:return '-'
        off=offset(address);end=dol.find(b'\0',off)
        if end<0 or end-off>255:raise ValueError('Invalid patch string')
        text=dol[off:end].decode('shift_jis')
        if any(c in text for c in '|\r\n'):raise ValueError('Unsafe table string')
        return text
    with zipfile.ZipFile(patch) as z:
        # Fixed ZIP hash is the allowlist. Still reject duplicate/traversal/symlink
        # entries before writing, and only write beneath this private extraction.
        names=z.namelist()
        if len(set(names))!=len(names):raise ValueError('Duplicate patch members')
        for info in z.infolist():
            parts=PurePosixPath(info.filename).parts
            if '\\' in info.filename or '..' in parts or PurePosixPath(info.filename).is_absolute() or (info.external_attr>>16)&0o170000==0o120000:
                raise ValueError('Unsafe patch entry')
        xml=ET.fromstring(z.read(PREFIX+'riivolution/KMGR.xml'))
        selected=[xml.find("patch[@id='%s']"%key) for key in ['KMGALL','KMGUEJ','KMGNTSC']]
        assert all(p is not None for p in selected)
        if any(p.is_symlink() for p in (root/'files').rglob('*')):raise ValueError('Linked extraction is not supported')
        actual={p.relative_to(root/'files').as_posix().casefold():p for p in (root/'files').rglob('*') if p.is_file()}
        overlays={};tables=[HEADER];sites=[]
        for group in selected:
            for elem in group:
                a=elem.attrib
                if elem.tag=='savegame':continue # Native isolated NAND; never import bundled progress.
                if elem.tag=='folder':
                    folder=PREFIX+a['external'].lstrip('/')+'/'
                    for name in names:
                        if name.startswith(folder) and not name.endswith('/'):
                            rel=a['disc'].lstrip('/')+'/'+name[len(folder):]
                            target=actual.get(rel.casefold(),root/'files'/rel)
                            target.parent.mkdir(parents=True,exist_ok=True)
                            target.write_bytes(z.read(name));overlays[rel]=sha(target.read_bytes())
                    continue
                if elem.tag!='memory':raise ValueError('Unexpected patch directive')
                addr=int(a['offset'],16);sites.append(a)
                if 'original' in a:
                    expected=bytes.fromhex(a['original'].removeprefix('0x'))
                    off=offset(addr,len(expected))
                    if dol[off:off+len(expected)]!=expected:raise ValueError('Source patch guard differs')
                if addr in [0x8053af78,0x8053b000,0x8053b088,0x8053b150]:
                    dome=[0x8053af78,0x8053b000,0x8053b088,0x8053b150].index(addr)+1
                    blob=bytes.fromhex(a['value'])
                    for i in range(0,len(blob),8):
                        ptr,category=struct.unpack_from('>II',blob,i)
                        if not ptr:break
                        tables.append('C|%d|%s|%d'%(dome,string(ptr),category))
                elif addr==0x8053BC94:
                    blob=z.read(PREFIX+a['valuefile'].lstrip('/'))
                    if len(blob)!=480:raise ValueError('Unexpected event table size')
                    for i in range(24):
                        name,typ,save,c1,c2,unused,p3,p4=struct.unpack_from('>IBBBBIII',blob,i*20)
                        index=121+i
                        # This guards native source-array index correspondence.
                        old_name=struct.unpack_from('>I',dol,offset(0x8053b320+index*20))[0]
                        tables.append('F|%d|%s|%s|%d|%d|%d|%d|%d|%s|%s'%(index,string(old_name),string(name),typ,save,c1,c2,unused,string(p3),string(p4)))
                elif addr==0x8053C800:
                    (root/'sys/KaizoGalaxyID.raw').write_bytes(z.read(PREFIX+a['valuefile'].lstrip('/')))
                elif addr>=0x80500000:raise ValueError('Unmapped patch data site')
                # Remaining 19 PPC code sites are NOT applied here. Engine
                # equivalents are per-profile and audited in PATCH-MAPPING.md.
        if len(sites)!=25:raise ValueError('Unexpected number of NTSC patch sites')
        (root/'sys/KaizoTables.txt').write_bytes(('\n'.join(tables)+'\n').encode('ascii'))
        (root/'sys/KaizoCredits.txt').write_bytes(z.read(PREFIX+'Credits.txt'))
    (root/'sys/fst.bin').write_bytes(rebuild_fst(root/'files'))
    receipt={'profile':'kmgr-2.1','patch_sha256':PATCH_SHA256,'base_dol_sha256':sha(dol),
             'overlay_files':overlays,'memory_sites':sites,'bundled_save_imported':False}
    (root/'sys/KaizoImport.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Kaizo Rebalanced 2.1: verified patch, %d overlays, 25 mapped memory sites'%len(overlays))

def finish(root,output):
    import bcsv
    root,output=Path(root),Path(output)
    table=bytearray((root/'sys/KaizoGalaxyID.raw').read_bytes());bcsv.swap(table)
    (output/'sys/GalaxyID.bcsv').write_bytes(table)
    for name in ['KaizoTables.txt','KaizoCredits.txt','KaizoImport.json']:
        shutil.copy2(root/'sys'/name,output/'sys'/name)
