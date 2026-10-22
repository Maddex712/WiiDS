import os, re

include_dirs = [
    'platform/include',
    'decomp/include',
    'decomp/libs/JSystem/include',
    'decomp/libs/RVL_SDK/include',
    'decomp/libs/nw4r/include',
    'decomp/libs/RVLFaceLib/include'
]

pattern = re.compile(r'^\s*#include\s+["<]([^">]+)[">]')
mismatches = []

for root, _, files in os.walk('decomp'):
    for file in files:
        if file.endswith(('.cpp', '.c', '.hpp', '.h')):
            p = os.path.join(root, file)
            with open(p, 'r', encoding='latin-1') as f:
                for line_num, line in enumerate(f, 1):
                    m = pattern.match(line)
                    if m:
                        inc = m.group(1).replace('\\', '/')
                        found = False
                        for idir in include_dirs:
                            candidate = os.path.join(idir, inc)
                            if os.path.isfile(candidate):
                                found = True
                                cur = idir
                                for part in inc.split('/'):
                                    entries = os.listdir(cur)
                                    matching = [e for e in entries if e.lower() == part.lower()]
                                    if matching and matching[0] != part:
                                        mismatches.append((p, line_num, inc, f"Actual on disk: {matching[0]} in {cur}"))
                                    cur = os.path.join(cur, part)
                                break

print(f"Total header case mismatches: {len(mismatches)}")
for m in mismatches:
    print(m)
