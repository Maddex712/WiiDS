"""GX texture decoding (BTI headers inside J3D TEX1), vectorised with numpy.

decode() turns GX tiled, big-endian pixel data into an (h, w, 4) uint8 RGBA array. Intensity
formats decode as grey with that alpha (I8 -> I, I, I, I), as the GX texture unit reads them.
"""
import struct
import numpy as np

# format: (block width, block height, bits per pixel)
BLOCKS = {0: (8, 8, 4), 1: (8, 4, 8), 2: (8, 4, 8), 3: (4, 4, 16), 4: (4, 4, 16), 5: (4, 4, 16),
          6: (4, 4, 32), 8: (8, 8, 4), 9: (8, 4, 8), 10: (4, 4, 16), 14: (8, 8, 4)}
WRAP_CLAMP, WRAP_REPEAT, WRAP_MIRROR = 0, 1, 2


def _565(v):
    v = v.astype(np.uint32)
    return np.stack([(v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31,
                     np.full_like(v, 255)], -1).astype(np.uint8)


def _5a3(v):
    v = v.astype(np.uint32)
    opaque = (v & 0x8000) != 0
    a = np.stack([(v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, np.full_like(v, 255)], -1)
    b = np.stack([(v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17, (v >> 12 & 7) * 255 // 7], -1)
    return np.where(opaque[..., None], a, b).astype(np.uint8)


def _ia8(v):
    v = v.astype(np.uint32)
    i, a = v & 255, v >> 8
    return np.stack([i, i, i, a], -1).astype(np.uint8)


def palette(d, off, count, fmt):
    v = np.frombuffer(d, '>u2', count=count, offset=off)
    return _ia8(v) if fmt == 0 else _565(v) if fmt == 1 else _5a3(v)


def _untile(px, w, h, bw, bh):
    """Pixels in block order (blocks row-major, pixels row-major in a block) -> (h, w, ...)."""
    nbx, nby = -(-w // bw), -(-h // bh)
    rest = px.shape[1:]
    px = px.reshape((nby, nbx, bh, bw) + rest).swapaxes(1, 2).reshape((nby * bh, nbx * bw) + rest)
    return px[:h, :w]


def decode(d, off, fmt, w, h, pal=None):
    bw, bh, bpp = BLOCKS[fmt]
    nbx, nby = -(-w // bw), -(-h // bh)
    npx = nbx * nby * bw * bh
    nbytes = npx * bpp // 8
    raw = np.frombuffer(d, np.uint8, count=nbytes, offset=off)
    if fmt == 14:  # CMPR: each 8x8 block is 2x2 DXT1 sub-blocks
        sub = raw.reshape(-1, 8)
        c0 = sub[:, 0].astype(np.uint32) << 8 | sub[:, 1]
        c1 = sub[:, 2].astype(np.uint32) << 8 | sub[:, 3]
        bits = (sub[:, 4].astype(np.uint32) << 24 | sub[:, 5].astype(np.uint32) << 16
                | sub[:, 6].astype(np.uint32) << 8 | sub[:, 7])
        a, b = _565(c0).astype(np.int32), _565(c1).astype(np.int32)
        four = c0 > c1
        p2 = np.where(four[:, None], (2 * a + b) // 3, (a + b) // 2)
        p3 = np.where(four[:, None], (a + 2 * b) // 3, 0)
        p2[:, 3] = 255
        p3[:, 3] = np.where(four, 255, 0)
        pal4 = np.stack([a, b, p2, p3], 1).astype(np.uint8)              # (n, 4, 4)
        idx = (bits[:, None] >> (30 - 2 * np.arange(16, dtype=np.uint32))) & 3  # (n, 16)
        px = np.take_along_axis(pal4, idx[:, :, None].astype(np.int64), 1)   # (n, 16, 4)
        px = px.reshape(nby, nbx, 2, 2, 4, 4, 4).transpose(0, 2, 4, 1, 3, 5, 6)
        return np.ascontiguousarray(px.reshape(nby * 8, nbx * 8, 4)[:h, :w])
    if fmt == 6:  # RGBA8: per 4x4 block, 32 bytes of AR then 32 of GB
        blk = raw.reshape(-1, 2, 16, 2)
        px = np.stack([blk[:, 0, :, 1], blk[:, 1, :, 0], blk[:, 1, :, 1], blk[:, 0, :, 0]], -1).reshape(-1, 4)
        return np.ascontiguousarray(_untile(px, w, h, 4, 4))
    if bpp == 4:
        v = np.stack([raw >> 4, raw & 15], -1).reshape(-1)
    elif bpp == 8:
        v = raw
    else:
        v = raw.reshape(-1, 2).astype(np.uint32)
        v = v[:, 0] << 8 | v[:, 1]
    if fmt == 0:
        i = (v * 17).astype(np.uint8)
        px = np.stack([i, i, i, i], -1)
    elif fmt == 1:
        px = np.stack([v, v, v, v], -1).astype(np.uint8)
    elif fmt == 2:
        i = ((v & 15) * 17).astype(np.uint8)
        px = np.stack([i, i, i, ((v >> 4) * 17).astype(np.uint8)], -1)
    elif fmt == 3:
        px = _ia8(v)
    elif fmt == 4:
        px = _565(v)
    elif fmt == 5:
        px = _5a3(v)
    elif fmt in (8, 9, 10):
        if fmt == 10:
            v = v & 0x3FFF
        if pal is None or len(pal) == 0:
            pal = np.full((1, 4), 200, np.uint8)
        px = pal[np.clip(v, 0, len(pal) - 1)]
    else:
        raise ValueError('format %d' % fmt)
    return np.ascontiguousarray(_untile(px, w, h, bw, bh))


def bti(d, h):
    """The BTI header at [h] (inside TEX1): its top mip as RGBA plus its wrap modes."""
    fmt, _alpha, w, hgt, ws, wt, _ix, pfmt, pcount, poff = struct.unpack_from('>BBHHBBBBHI', d, h)
    img = h + struct.unpack_from('>I', d, h + 0x1C)[0]
    pal = palette(d, h + poff, pcount, pfmt) if fmt in (8, 9, 10) and pcount else None
    return decode(d, img, fmt, w, hgt, pal), ws, wt, fmt
