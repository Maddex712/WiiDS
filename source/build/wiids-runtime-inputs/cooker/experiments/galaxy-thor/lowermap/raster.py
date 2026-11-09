"""Tiny numpy z-buffer rasterizer (flat-shaded triangles)."""
import numpy as np


def rasterize(P, colors, W, H, bg=(0, 0, 0), zbuf=None, cbuf=None):
    """P: (N,3,3) screen-space (x px, y px, depth; smaller depth = nearer). colors: (N,3) 0..255."""
    if zbuf is None:
        zbuf = np.full((H, W), np.inf)
        cbuf = np.zeros((H, W, 3), np.float64)
        cbuf[:] = bg
    for t in range(len(P)):
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = P[t]
        minx = max(int(np.floor(min(x0, x1, x2))), 0)
        maxx = min(int(np.ceil(max(x0, x1, x2))), W - 1)
        miny = max(int(np.floor(min(y0, y1, y2))), 0)
        maxy = min(int(np.ceil(max(y0, y1, y2))), H - 1)
        if minx > maxx or miny > maxy:
            continue
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-9:
            continue
        xs = np.arange(minx, maxx + 1) + 0.5
        ys = np.arange(miny, maxy + 1) + 0.5
        X, Y = np.meshgrid(xs, ys)
        w0 = ((x1 - X) * (y2 - Y) - (x2 - X) * (y1 - Y)) / area
        w1 = ((x2 - X) * (y0 - Y) - (x0 - X) * (y2 - Y)) / area
        w2 = 1.0 - w0 - w1
        m = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
        if not m.any():
            continue
        z = w0 * z0 + w1 * z1 + w2 * z2
        sub = zbuf[miny:maxy + 1, minx:maxx + 1]
        upd = m & (z < sub)
        sub[upd] = z[upd]
        cbuf[miny:maxy + 1, minx:maxx + 1][upd] = colors[t]
    return zbuf, cbuf


def downsample(img, s):
    H, W = img.shape[:2]
    return img[:H // s * s, :W // s * s].reshape(H // s, s, W // s, s, -1).mean(axis=(1, 3))
