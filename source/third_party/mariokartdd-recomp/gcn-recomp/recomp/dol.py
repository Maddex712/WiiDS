"""DOL loader, disc image reader and dtk symbols.txt parser."""
import re
import struct


class DiscImage:
    """Positional reads from a GameCube disc image: a plain .iso, or a CISO.

    CISO is a 0x8000-byte header -- "CISO", a little-endian block size, then one byte
    per block saying whether that block is stored -- followed by the stored blocks in
    order. Blocks that are not stored read as zero.
    """

    def __init__(self, path):
        self.f = open(path, 'rb')
        hdr = self.f.read(0x8000)
        self.block = 0
        if hdr[:4] == b'CISO':
            self.block = struct.unpack('<I', hdr[4:8])[0]
            self.map = {}
            stored = 0
            for i, present in enumerate(hdr[8:]):
                if present == 1:
                    self.map[i] = stored
                    stored += 1

    def read(self, offset, n):
        if not self.block:
            self.f.seek(offset)
            return self.f.read(n)
        out = bytearray()
        while n:
            blk, off = divmod(offset, self.block)
            k = min(n, self.block - off)
            if blk in self.map:
                self.f.seek(0x8000 + self.map[blk] * self.block + off)
                out += self.f.read(k)
            else:
                out += bytes(k)
            offset += k
            n -= k
        return bytes(out)


class Dol:
    def __init__(self, path):
        data = open(path, 'rb').read()
        self.data = data
        offs = struct.unpack('>18I', data[0x00:0x48])
        addrs = struct.unpack('>18I', data[0x48:0x90])
        sizes = struct.unpack('>18I', data[0x90:0xD8])
        self.bss_addr, self.bss_size, self.entry = struct.unpack('>3I', data[0xD8:0xE4])
        self.sections = []  # (addr, size, file_offset, is_text)
        for i in range(18):
            if sizes[i]:
                self.sections.append((addrs[i], sizes[i], offs[i], i < 7))

    def read32(self, addr):
        for a, s, o, _ in self.sections:
            if a <= addr < a + s:
                return struct.unpack_from('>I', self.data, o + addr - a)[0]
        raise KeyError(hex(addr))

    def read_bytes(self, addr, n):
        for a, s, o, _ in self.sections:
            if a <= addr and addr + n <= a + s:
                return self.data[o + addr - a:o + addr - a + n]
        raise KeyError(hex(addr))

    def in_text(self, addr):
        return any(t and a <= addr < a + s for a, s, _, t in self.sections)

    def text_sections(self):
        return [(a, s, o) for a, s, o, t in self.sections if t]


SYM_RE = re.compile(r'^(\S+) = (\.\w+):0x([0-9A-Fa-f]+); // (.*)$')


def load_symbols(path):
    """Returns list of dicts: name, section, addr, type, size."""
    out = []
    for line in open(path):
        m = SYM_RE.match(line.strip())
        if not m:
            continue
        name, sect, addr, rest = m.groups()
        attrs = {}
        for tok in rest.split():
            if ':' in tok:
                k, v = tok.split(':', 1)
                attrs[k] = v
        out.append(dict(name=name, section=sect, addr=int(addr, 16),
                        type=attrs.get('type'),
                        size=int(attrs['size'], 16) if 'size' in attrs else None))
    return out
