"""Generate the private ARM64 module inputs from the exact supported disc."""
import csv
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys


root = Path(__file__).resolve().parent
project = root / "colosseum-recomp"
iso = Path(sys.argv[1])
compiler = Path(sys.argv[2])
outputs = root / "module-source"
with iso.open("rb") as stream:
    if hashlib.file_digest(stream, "sha256").hexdigest() != "78c4074429fa598b97bfac296bf8e0f7e93f5cb6d8679f8d2653ca902820f6c5":
        raise SystemExit("The disc does not match the supported USA GC6E01 revision.")
    stream.seek(0x420)
    dol_offset, fst_offset = struct.unpack(">II", stream.read(8))
    stream.seek(dol_offset)
    dol = bytearray(stream.read(fst_offset - dol_offset))

def section_positions(data):
    offsets = struct.unpack_from(">18I", data, 0)
    addresses = struct.unpack_from(">18I", data, 0x48)
    sizes = struct.unpack_from(">18I", data, 0x90)
    return [(offset, address, size) for offset, address, size in zip(offsets, addresses, sizes) if size]


sections = section_positions(dol)
del dol[max(offset + size for offset, _, size in sections):]
with (project / "patches/GC6E01-native-widescreen.csv").open(newline="") as stream:
    patches = list(csv.DictReader(stream))
for row in patches:
    address = int(row["address"], 16)
    offsets = [offset + (address - start) for offset, start, size in sections if start <= address <= start + size - 4]
    if len(offsets) != 1:
        raise ValueError(f"DOL patch address outside a unique section: {address:08x}")
    (offset,) = offsets
    expected = bytes.fromhex(row["expected"])
    if dol[offset:offset + 4] != expected:
        raise ValueError(f"Unexpected word at {address:08x}")
    dol[offset:offset + 4] = bytes.fromhex(row["replacement"])

with (project / "patches/GC6E01-native-substitutions.csv").open(newline="") as stream:
    substitutions = ",".join(f'{row["guest"]}:{row["symbol"]}' for row in csv.DictReader(stream))
outputs.mkdir(exist_ok=True)
patched = outputs / "patched-main.dol"
patched.write_bytes(dol)
subprocess.run([str(compiler), "-j6", "--backend=c", "--cpu", "gekko", "--gamecube", str(patched), str(outputs)],
               check=True, env={**os.environ, "DOLRECOMP_NATIVE_SUBST": substitutions})
(outputs / "generated/main.dol").write_bytes(dol)
