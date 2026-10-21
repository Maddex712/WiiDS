#!/usr/bin/env python3
"""Scan every decomp unit for one g++ warning class (default: returning the
address of a local, which g++ compiles to a NULL return).
usage: tools/warn_scan.py [-Wflag] [regex]  (uses build-gx/compile_commands.json)"""
import json, subprocess, re, os, sys
from concurrent.futures import ThreadPoolExecutor
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
flag = sys.argv[1] if len(sys.argv) > 1 else '-Wreturn-local-addr'
pat = sys.argv[2] if len(sys.argv) > 2 else r'(reference|address) to local variable .* returned|address of local variable .* returned|returning (reference|address) (to|of) (temporary|local)'
cc = json.load(open(os.path.join(root, os.environ.get('SMS_BUILD', 'build'), 'compile_commands.json')))
cc = [e for e in cc if '/src/' in e['file'] and '/platform/' not in e['file']]
def run(e):
    cmd = re.sub(r'\s-o\s+\S+', ' ', e['command']).replace(' -w ', ' ') + ' -fsyntax-only ' + flag
    r = subprocess.run('nice -n 19 ' + cmd, shell=True, cwd=e['directory'], capture_output=True, text=True)
    return set(m.group(1) + ': ' + m.group(2) for m in re.finditer(r'^(\S+?:\d+):\d+: warning: (.*)$', r.stderr, re.M) if re.search(pat, m.group(2)))
res = set()
with ThreadPoolExecutor(2) as ex:
    for s in ex.map(run, cc):
        res |= s
for l in sorted(res):
    print(l.replace(root + '/', ''))
