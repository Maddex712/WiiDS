#!/usr/bin/env python3
"""Writes the game's global data ranges into the linked executable, for save states.

    savestate_ranges.py --map sms.map --exe sms [--archive libsms_game.a ...]

A save state copies the game's own global variables (platform/savestate), and
only the link map says where the linker put them. This reads the map (GNU ld
on Linux and Windows, ld64 on macOS), keeps the writable sections that came
from the game's archives, merges the neighbours, and writes them with a
fingerprint of the build into the placeholder table in the executable (found by
its magic; kRanges in platform/savestate/savestate.cpp). The fingerprint is a
SHA-256 of the executable with the table blank, so two identical builds share
it and any other build differs. Running it again gives the same result.
"""

import argparse
import hashlib
import os
import re
import struct
import sys

MAGIC = b"SMSSTATE-RANGES\0"
MAX_RANGES = 2048
TABLE_SIZE = 16 + 32 + 8 + MAX_RANGES * 16

# Output sections that hold writable data (not .data.rel.ro: constant once loaded).
GNU_WRITABLE = re.compile(r"^\.(data|data1|bss|sdata|sbss)$")
NUM = r"0x[0-9a-fA-F]+"


def from_game(path, archives):
    """A member of one of the archives: GNU ld and classic ld64 write lib.a(obj.o),
    Apple's newer linker lib.a[12](obj.o)."""
    return any(re.search(re.escape(name) + r"(\[\d+\])?\(", path) for name in archives)


def gnu_ranges(lines, archives):
    """[start, end) of each game input section in a writable output section."""
    out = []
    section = None
    previous_game = False
    pending_name = None
    for line in lines:
        line = line.rstrip("\n")
        if not line:
            continue
        if not line[0].isspace():
            # an output section: ".data   0x... 0x..." or just ".data"
            section = line.split()[0]
            previous_game = False
            pending_name = None
            continue
        if section is None or not GNU_WRITABLE.match(section):
            continue
        fields = line.split()
        if fields[0] == "*fill*" and len(fields) >= 3:
            # padding after a game section belongs with it
            if previous_game and out:
                start, size = int(fields[1], 16), int(fields[2], 16)
                if start == out[-1][1]:
                    out[-1][1] = start + size
            continue
        if fields[0].startswith("*("):
            continue
        # an input section, its fields maybe on the next line when its name is long
        if len(fields) == 1 and not re.fullmatch(NUM, fields[0]):
            pending_name = fields[0]
            continue
        if pending_name and re.fullmatch(NUM, fields[0]):
            fields = [pending_name] + fields
        pending_name = None
        if len(fields) >= 4 and re.fullmatch(NUM, fields[1]) and re.fullmatch(NUM, fields[2]):
            name, start, size, path = fields[0], int(fields[1], 16), int(fields[2], 16), " ".join(fields[3:])
            if name.startswith(".") or name == "COMMON":
                previous_game = from_game(path, archives)
                if previous_game and size:
                    out.append([start, start + size])
            continue
        # symbol lines ("0x...   name") leave the state as it is
    return out


def ld64_ranges(lines, archives):
    """ld64 -map: game symbols that sit in a writable __DATA section."""
    files, sections, symbols = {}, [], []
    part = None
    for line in lines:
        line = line.rstrip("\n")
        if line.startswith("# Object files:"):
            part = "files"
            continue
        if line.startswith("# Sections:"):
            part = "sections"
            continue
        if line.startswith("# Symbols:"):
            part = "symbols"
            continue
        if line.startswith("#") or not line.strip():
            continue
        if part == "files":
            m = re.match(r"\[\s*(\d+)\]\s+(.*)$", line)
            if m:
                files[int(m.group(1))] = m.group(2)
        elif part == "sections":
            f = line.split()
            if len(f) >= 4 and f[2].startswith("__DATA") and f[3] in ("__data", "__bss", "__common"):
                start, size = int(f[0], 16), int(f[1], 16)
                sections.append((start, start + size))
        elif part == "symbols":
            m = re.match(r"(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)\s+\[\s*(\d+)\]", line)
            if m:
                symbols.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(3))))
    out = []
    for start, size, index in symbols:
        if size and from_game(files.get(index, ""), archives) and any(a <= start < b for a, b in sections):
            out.append([start, start + size])
    return out


def merge(ranges):
    ranges = sorted(ranges)
    merged = []
    for start, end in ranges:
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return merged


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--map", required=True)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--archive", action="append", default=[])
    args = parser.parse_args()
    archives = args.archive or ["libsms_game.a"]

    with open(args.map, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    ld64 = any(line.startswith("# Object files:") for line in lines[:50])
    ranges = merge(ld64_ranges(lines, archives) if ld64 else gnu_ranges(lines, archives))
    if len(ranges) > MAX_RANGES:
        sys.exit(f"savestate_ranges: {len(ranges)} ranges, more than the table's {MAX_RANGES}")

    with open(args.exe, "rb") as f:
        image = bytearray(f.read())
    at = image.find(MAGIC)
    if at < 0 or image.find(MAGIC, at + 1) >= 0:
        sys.exit(f"savestate_ranges: {'no' if at < 0 else 'more than one'} range table in {args.exe}")
    blank = bytes(TABLE_SIZE - len(MAGIC))
    image[at + len(MAGIC):at + TABLE_SIZE] = blank
    fingerprint = hashlib.sha256(image).digest()
    table = fingerprint + struct.pack("<II", len(ranges), 0)
    table += b"".join(struct.pack("<QQ", start, end) for start, end in ranges)
    image[at + len(MAGIC):at + len(MAGIC) + len(table)] = table

    temporary = args.exe + ".ranges"
    with open(temporary, "wb") as f:
        f.write(image)
    os.chmod(temporary, os.stat(args.exe).st_mode)
    os.replace(temporary, args.exe)
    total = sum(end - start for start, end in ranges)
    print(f"savestate_ranges: {len(ranges)} ranges, {total} bytes of game data, build {fingerprint.hex()[:16]}")
    if not ranges:
        # the game still builds, but its save states can only load the portable way
        print(f"savestate_ranges: warning: no game data found in {args.map}; "
              "save states will load the position only", file=sys.stderr)


if __name__ == "__main__":
    main()
