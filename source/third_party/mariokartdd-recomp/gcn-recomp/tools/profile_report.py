#!/usr/bin/env python3
"""Summarise a GCN_PROFILE histogram (runtime/host_profile.cpp) by function.

    profile_report.py <profile.txt> <unstripped binary> [--addr2line=PATH] [--top=N]

Symbolises each `module+offset` in the binary's own module with llvm-addr2line (the NDK
ships one) and prints the functions taking the most samples, overall and per thread.
Samples in other modules (libc, the GL driver) are summed by module.
"""
import collections
import os
import subprocess
import sys


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    opts = dict(a[2:].split('=', 1) for a in sys.argv[1:] if a.startswith('--') and '=' in a)
    if len(args) != 2:
        sys.exit(__doc__)
    prof, binary = args
    addr2line = opts.get('addr2line', 'llvm-addr2line')
    top = int(opts.get('top', '40'))
    module = os.path.basename(binary)

    rows = []
    for line in open(prof):
        if line.startswith('#'):
            continue
        count, tid, loc = line.split()
        mod, off = loc.rsplit('+', 1)
        rows.append((int(count), int(tid), mod, int(off, 16)))
    total = sum(r[0] for r in rows)

    offsets = sorted({r[3] for r in rows if r[2] == module})
    names = {}
    chunk = 2000
    if 'symbolizer' in opts:
        # --symbolizer=<llvm-symbolizer> with --outer names each sample by the outermost
        # function of its inline chain: the recompiled function a load was inlined into,
        # rather than LD32.
        import json
        outer = '--outer' in sys.argv or opts.get('outer') == '1'
        for i in range(0, len(offsets), chunk):
            part = offsets[i:i + chunk]
            out = subprocess.run([opts['symbolizer'], '--output-style=JSON', '--demangle',
                                  '--obj=' + binary] + [hex(o) for o in part],
                                 capture_output=True, text=True).stdout
            entries = []
            for line in out.splitlines():
                if not line.strip():
                    continue
                v = json.loads(line)
                entries.extend(v if isinstance(v, list) else [v])
            for j, e in enumerate(entries[:len(part)]):
                frames = e.get('Symbol', [])
                fr = frames[-1] if (outer and frames) else (frames[0] if frames else {})
                names[part[j]] = fr.get('FunctionName', '?') or '?'
    else:
        for i in range(0, len(offsets), chunk):
            part = offsets[i:i + chunk]
            out = subprocess.run([addr2line, '-f', '-C', '-e', binary] + [hex(o) for o in part],
                                 capture_output=True, text=True).stdout.splitlines()
            for j, o in enumerate(part):
                names[o] = out[2 * j] if 2 * j < len(out) else '?'

    by_fn = collections.Counter()
    by_tid = collections.defaultdict(collections.Counter)
    for count, tid, mod, off in rows:
        name = names.get(off, '?') if mod == module else f'[{mod}]'
        by_fn[name] += count
        by_tid[tid][name] += count

    print(f'{total} samples')
    for name, n in by_fn.most_common(top):
        print(f'{100.0 * n / total:6.2f}%  {n:7d}  {name}')
    for tid, c in sorted(by_tid.items(), key=lambda kv: -sum(kv[1].values())):
        n = sum(c.values())
        if n < total / 50:
            continue
        print(f'\nthread {tid}: {100.0 * n / total:.1f}%')
        for name, k in c.most_common(15):
            print(f'{100.0 * k / n:6.2f}%  {name}')


if __name__ == '__main__':
    main()
