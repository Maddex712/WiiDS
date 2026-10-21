#!/usr/bin/env python3
"""Fit the frsqrte / fres estimate tables from a probe trace (see build_dol.py).
Prints (base, dec) per segment; asserts the piecewise-linear model is exact."""
import struct, sys

def last(path):
    out = {}
    for line in open(path):
        if line.startswith('D '):
            _, l, a, d = line.rstrip('\n').split(' ', 3)
            out[l] = bytes.fromhex(d)
    return out

D = last(sys.argv[1])
M = (1 << 52) - 1
rsq = [struct.unpack('>Q', D['rsq'][8 * i:8 * i + 8])[0] for i in range(65536)]
res = [struct.unpack('>Q', D['res'][8 * i:8 * i + 8])[0] for i in range(32768)]
rs = []
for s in range(32):
    ms = [(rsq[s * 2048 + j] & M) >> 26 for j in range(2048)]
    B, Dd = ms[0], ms[0] - ms[1]
    assert all(ms[j] == B - Dd * j for j in range(2048)), s
    assert all((rsq[s * 2048 + j] & ~M) == (1022 << 52) for j in range(2048))
    rs.append((B, Dd))
re_ = []
for s in range(32):
    ms = [(res[s * 1024 + j] & M) >> 29 for j in range(1024)]
    B = ms[0]
    Dd = next(d for d in range(1, 4000) if all(ms[j] == B - ((d * j + 1) >> 1) for j in range(1024)))
    re_.append((B, Dd))
print('frsqrte', rs)
print('fres', re_)
