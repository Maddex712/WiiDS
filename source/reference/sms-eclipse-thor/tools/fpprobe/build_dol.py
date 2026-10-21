#!/usr/bin/env python3
"""Build probe.dol: assembles probe.s (needs a powerpc-eabi binutils:
$PPC_BINUTILS, default the decomp's build/binutils) and appends 4096 random test inputs
(seeded, so inputs.bin is reproducible) as the DOL's data section at 0x80200000.

Then run it in the dolphin-oracle emulator:
  cd $DOLPHIN_ORACLE && scripts/run_oracle.py --game <this dir>/probe.dol \
      --out runs/fpprobe-interp --cpu interp --frames 90 --start 80 --no-memcard \
      --ranges <this dir>/ranges.txt
and check src/port_fpu.h: fit.py <trace> prints the tables, fpu_test.c compares."""
import os, random, struct, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
B = os.environ.get('PPC_BINUTILS', os.path.join(here, '..', '..', 'decomp', 'build', 'binutils'))
out = sys.argv[1] if len(sys.argv) > 1 else here
run = lambda *a: subprocess.check_call(list(a))
run(B + '/powerpc-eabi-as', '-mgekko', '-o', out + '/probe.o', here + '/probe.s')
run(B + '/powerpc-eabi-ld', '-Ttext=0x80003100', '-o', out + '/probe.elf', out + '/probe.o')
run(B + '/powerpc-eabi-objcopy', '-O', 'binary', '-j', '.text', out + '/probe.elf', out + '/probe.bin')

random.seed(1234)
vals = []
for i in range(4096):
    kind = i % 4
    if kind == 0:
        v = random.uniform(1e-3, 1e6)
    elif kind == 1:
        v = random.uniform(0.5, 2.0)
    elif kind == 2:
        v = random.choice([1.0, 2.0, 4.0, 0.25, 215.8703 ** 2 / 2, 3.0, 100.0, 1e-20, 1e20, -1.0, 0.0,
                           float('inf')]) if i < 48 else 2.0 ** random.uniform(-60, 60)
    else:
        v = struct.unpack('>f', struct.pack('>f', random.uniform(0.001, 5000)))[0]
    vals.append(v)
data = b''.join(struct.pack('>d', v) for v in vals)
open(out + '/inputs.bin', 'wb').write(data)
text = open(out + '/probe.bin', 'rb').read()
text += b'\0' * ((-len(text)) % 32)
hdr = bytearray(0x100)
struct.pack_into('>I', hdr, 0x00, 0x100)                 # text0 offset
struct.pack_into('>I', hdr, 0x1c, 0x100 + len(text))     # data0 offset
struct.pack_into('>I', hdr, 0x48, 0x80003100)            # text0 address
struct.pack_into('>I', hdr, 0x64, 0x80200000)            # data0 address
struct.pack_into('>I', hdr, 0x90, len(text))
struct.pack_into('>I', hdr, 0xac, len(data))
struct.pack_into('>III', hdr, 0xd8, 0x80300000, 0x1000, 0x80003100)  # bss, bss size, entry
open(out + '/probe.dol', 'wb').write(bytes(hdr) + text + data)
open(out + '/ranges.txt', 'w').write('0x80100000 0x80000 rsq\n0x80180000 0x40000 res\n0x801C0000 0x8000 rrsq\n'
                                     '0x801C8000 0x8000 rres\n0x801D0000 0x4 done\n')
print('wrote', out + '/probe.dol')
