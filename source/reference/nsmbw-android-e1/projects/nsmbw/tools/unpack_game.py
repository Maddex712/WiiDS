"""Prepare the translator's inputs from New Super Mario Bros. Wii (SMNE01 rev 1) extracted by Dolphin.

    python unpack_game.py <extracted game folder> [<output folder>]

The extracted folder is what Dolphin's Extract Entire Disc makes (files/ and sys/, or a DATA folder
holding them). This copies sys/main.dol and unpacks the four boot RELs (files/rels/*.rel.LZ, LZ77
type 0x11) into the output folder: by default the "game" folder next to this repository, where
recomp.yml looks for them. Then run prelink_rels.py. Standard library only.
"""
import hashlib
import shutil
import struct
import sys
from pathlib import Path

RELS = ("d_profileNP", "d_basesNP", "d_enemiesNP", "d_en_bossNP")
# Thor clean E1 profile: independently DTK/NKit extracted from the owner's Redump-verified disc.
# Upstream assumed 6d755ec24c395fee600a856a85785e0e99c0b7ecb680d573a6cdd1aa6f7beee7; its differing bytes are not established.
# The corrected pin requires an entirely new translation; never reuse the upstream generated game code.
MAIN_DOL_SHA256 = "166eb6b7cca21418e316a6cfbf1bfbf0a213eb8f6396d42126a1482bb764a955"
REL_SHA256 = {
    "d_profileNP": "f04ca9636e20feb5bdacdb8a1115a2b4a71e99f5091df56b4099ae8ea36c7eaf",
    "d_basesNP": "66b51efac566aead05e4b2351f64800ca54cd38aa93d8d1c9961a054f2854ebb",
    "d_enemiesNP": "9896eab47a57ee0d3e2644393dd4c35e81171662da020fdb6bebc9307a4a9283",
    "d_en_bossNP": "ca28f19ed9fa1a16d5bca7260e9e0127ecd0a964697531da16747d8edffac124",
}
DEFAULT_OUT = Path(__file__).resolve().parents[3].parent / "game"


def lz11(data: bytes) -> bytes:
    if data[0] != 0x11:
        raise ValueError("not LZ77 type 0x11 data")
    size = data[1] | data[2] << 8 | data[3] << 16
    pos = 4
    if size == 0:
        size = struct.unpack_from("<I", data, 4)[0]
        pos = 8
    out = bytearray()
    while len(out) < size:
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if not flags & (0x80 >> bit):
                out.append(data[pos])
                pos += 1
                continue
            b0 = data[pos]
            if b0 >> 4 == 0:
                b1, b2 = data[pos + 1], data[pos + 2]
                length = ((b0 & 0xF) << 4 | b1 >> 4) + 0x11
                disp = ((b1 & 0xF) << 8 | b2) + 1
                pos += 3
            elif b0 >> 4 == 1:
                b1, b2, b3 = data[pos + 1], data[pos + 2], data[pos + 3]
                length = ((b0 & 0xF) << 12 | b1 << 4 | b2 >> 4) + 0x111
                disp = ((b2 & 0xF) << 8 | b3) + 1
                pos += 4
            else:
                b1 = data[pos + 1]
                length = (b0 >> 4) + 1
                disp = ((b0 & 0xF) << 8 | b1) + 1
                pos += 2
            start = len(out) - disp
            for i in range(length):  # the source may overlap what is being written
                out.append(out[start + i])
    return bytes(out[:size])


def find_root(path: Path) -> Path:
    for candidate in (path, path / "DATA"):
        if (candidate / "files").is_dir() and (candidate / "sys" / "main.dol").is_file():
            return candidate
    sys.exit(f"{path} is not an extracted game: it needs files/ and sys/ (Dolphin's Extract Entire Disc)")


def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    root = find_root(Path(sys.argv[1]))
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_OUT
    header = (root / "sys" / "boot.bin").read_bytes()[:8]
    if len(header) != 8:
        sys.exit("sys/boot.bin is incomplete; USA SMNE01 revision 1 is required")
    if header[:6] != b"SMNE01" or header[7] != 1:
        sys.exit(f"this is {header[:6].decode('ascii', 'replace')} revision {header[7]}; "
                 "the port needs New Super Mario Bros. Wii SMNE01 revision 1")
    dol = (root / "sys" / "main.dol").read_bytes()
    if hashlib.sha256(dol).hexdigest() != MAIN_DOL_SHA256:
        sys.exit("sys/main.dol does not match the canonical clean E1 input profile; do not reuse stale generated code")
    compressed_rels = {name: (root / "files" / "rels" / f"{name}.rel.LZ").read_bytes() for name in RELS}
    for name, compressed in compressed_rels.items():
        if hashlib.sha256(compressed).hexdigest() != REL_SHA256[name]:
            sys.exit(f"{name}.rel.LZ does not match the canonical clean E1 input profile")
    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / "sys" / "main.dol", out / "main.dol")
    for name in RELS:
        rel = lz11(compressed_rels[name])
        (out / f"{name}.rel").write_bytes(rel)
        print(f"{name}.rel  {len(rel)} bytes")
    print(f"main.dol and the RELs are in {out}; next: prelink_rels.py")


if __name__ == "__main__":
    main()
