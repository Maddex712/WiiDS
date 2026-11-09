"""SMG KCL reader -> numpy triangles (big-endian, Wii)."""
import struct
import numpy as np


def parse(data):
    pos_off, nrm_off, pri_off, oct_off = struct.unpack_from('>IIII', data, 0)
    thickness = struct.unpack_from('>f', data, 0x10)[0]
    origin = struct.unpack_from('>fff', data, 0x14)
    masks = struct.unpack_from('>III', data, 0x20)
    shifts = struct.unpack_from('>III', data, 0x2C)
    hdr = dict(pos_off=pos_off, nrm_off=nrm_off, pri_off=pri_off, oct_off=oct_off,
               thickness=thickness, origin=origin, masks=['%08X' % m for m in masks], shifts=shifts)
    if pos_off >= 0x3C:
        hdr['sphere_radius'] = struct.unpack_from('>f', data, 0x38)[0]
    npos = (nrm_off - pos_off) // 12
    nnrm = (pri_off + 0x10 - nrm_off) // 12
    pos = np.frombuffer(data, dtype='>f4', count=npos * 3, offset=pos_off).reshape(-1, 3).astype(np.float64)
    nrm = np.frombuffer(data, dtype='>f4', count=nnrm * 3, offset=nrm_off).reshape(-1, 3).astype(np.float64)
    first = pri_off + 0x10
    nprism = (oct_off - first) // 0x10
    rec = np.frombuffer(data, dtype=np.dtype([('h', '>f4'), ('p', '>u2'), ('fn', '>u2'), ('e1', '>u2'),
                                               ('e2', '>u2'), ('e3', '>u2'), ('attr', '>u2')]),
                        count=nprism, offset=first)
    h = rec['h'].astype(np.float64)
    A = pos[rec['p']]
    fn = nrm[rec['fn']]
    n1 = nrm[rec['e1']]
    n2 = nrm[rec['e2']]
    n3 = nrm[rec['e3']]
    ca = np.cross(n1, fn)
    cb = np.cross(n2, fn)
    with np.errstate(divide='ignore', invalid='ignore'):
        B = A + cb * (h / np.einsum('ij,ij->i', cb, n3))[:, None]
        C = A + ca * (h / np.einsum('ij,ij->i', ca, n3))[:, None]
    tris = np.stack([A, B, C], axis=1)
    ok = np.isfinite(tris).all(axis=(1, 2))
    hdr.update(npos=npos, nnrm=nnrm, nprism=nprism, bad=int((~ok).sum()))
    return hdr, tris[ok], fn[ok], rec['attr'][ok]
