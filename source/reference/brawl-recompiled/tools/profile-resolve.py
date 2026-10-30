# Resolve profile_stacks.txt (written by NSMBW_PROFILE runs, in the game's working dir) to function names.
# Usage: python tools\profile-resolve.py <profile_stacks.txt> [exe]   -> self and inclusive % per host function.
import bisect, collections, subprocess, sys

stacks_path = sys.argv[1]
exe = sys.argv[2] if len(sys.argv) > 2 else 'wiicompiled/native-build/WiiCompiled.exe'
base = 0x140000000
syms = []
for line in subprocess.run([r'C:\msys64\mingw64\bin\nm.exe', '-C', '--defined-only', exe],
                           capture_output=True, text=True, errors='replace').stdout.splitlines():
    parts = line.split(' ', 2)
    if len(parts) == 3 and parts[1] in 'tT':
        syms.append((int(parts[0], 16) - base, parts[2]))
syms.sort()
addrs = [a for a, _ in syms]

def name(rva):
    i = bisect.bisect_right(addrs, rva) - 1
    return syms[i][1][:110] if i >= 0 else '?'

self_c, incl = collections.Counter(), collections.Counter()
total = 0
for line in open(stacks_path):
    frames = line.split()
    if not frames:
        continue
    total += 1
    self_c['[dll]' if frames[0] == '-' else name(int(frames[0], 16))] += 1
    for n in {name(int(f, 16)) for f in frames if f != '-'}:
        incl[n] += 1
assert total, 'no samples'
print(f'{total} samples\n-- self --')
for n, c in self_c.most_common(40):
    print(f'{100 * c / total:5.1f}%  {n}')
print('-- inclusive --')
for n, c in incl.most_common(60):
    print(f'{100 * c / total:5.1f}%  {n}')
if len(sys.argv) > 3:  # callers of functions whose name contains argv[3]
    callers = collections.Counter()
    for line in open(stacks_path):
        names = [name(int(f, 16)) for f in line.split() if f != '-']
        for i, n in enumerate(names[:-1]):
            if sys.argv[3] in n:
                callers[' < '.join(names[i:i + 4])] += 1
                break
    print(f'-- callers of {sys.argv[3]} --')
    for n, c in callers.most_common(15):
        print(f'{100 * c / total:5.1f}%  {n}')
