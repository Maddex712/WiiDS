"""Extract selected owner-derived functions from a verified source bundle, into ignored build/."""
import argparse
import hashlib
from pathlib import Path
import struct


def read_selected(bundle, addresses):
    result={}
    with Path(bundle).open("rb") as stream:
        assert stream.read(8)==b"MKWSRC01"
        version,count=struct.unpack("<II",stream.read(8)); assert version==1
        for _ in range(count):
            addr,n=struct.unpack("<II",stream.read(8))
            assert n<=1<<20
            path=stream.read(n).decode()
            length,=struct.unpack("<I",stream.read(4)); expected=stream.read(32)
            if addr not in addresses:
                stream.seek(length,1); continue
            source=stream.read(length)
            assert len(source)==length and hashlib.sha256(source).digest()==expected
            result[addr]=(path,source.decode())
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("addresses",type=lambda s:int(s,16),nargs="+")
    args=parser.parse_args()
    assert "build" in args.output.resolve().parts
    args.output.mkdir(parents=True,exist_ok=True)
    selected=read_selected(args.bundle,set(args.addresses))
    for addr,(_,source) in selected.items():
        (args.output/f"func_{addr:08x}.cpp").write_text(source)
        print(f"0x{addr:08X}: verified {len(source)} source characters")
    missing=set(args.addresses)-selected.keys()
    if missing: raise SystemExit("Missing: "+", ".join(f"{a:08X}" for a in missing))


if __name__=="__main__": main()
