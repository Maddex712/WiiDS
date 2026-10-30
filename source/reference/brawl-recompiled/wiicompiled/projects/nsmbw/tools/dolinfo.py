"""Print a DOL's section layout and the r1/r2/r13 constants installed by __init_registers."""
import struct
import sys

import capstone

path = sys.argv[1]
data = open(path, "rb").read()
offs = struct.unpack(">18I", data[0x00:0x48])
addrs = struct.unpack(">18I", data[0x48:0x90])
sizes = struct.unpack(">18I", data[0x90:0xD8])
bss_addr, bss_size, entry = struct.unpack(">3I", data[0xD8:0xE4])

sections = []
for i in range(18):
    if sizes[i]:
        kind = "text" if i < 7 else "data"
        sections.append((kind, i, offs[i], addrs[i], sizes[i]))
        print(f"{kind}{i if i < 7 else i - 7}: file 0x{offs[i]:06X} addr 0x{addrs[i]:08X}-0x{addrs[i]+sizes[i]:08X} size 0x{sizes[i]:X}")
print(f"bss: 0x{bss_addr:08X}-0x{bss_addr+bss_size:08X} size 0x{bss_size:X}")
print(f"entry: 0x{entry:08X}")


def read(addr, n):
    for _, _, off, a, s in sections:
        if a <= addr < a + s:
            return data[off + addr - a: off + addr - a + n]
    raise KeyError(hex(addr))


md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)


def disasm(addr, count):
    return list(md.disasm(read(addr, count * 4), addr))


print("--- entry")
for ins in disasm(entry, 12):
    print(f"  {ins.address:08X}: {ins.mnemonic} {ins.op_str}")
first_bl = next(i for i in disasm(entry, 32) if i.mnemonic == "bl")
target = int(first_bl.op_str, 16)
print(f"--- first bl -> 0x{target:08X} (__init_registers)")
regs = {}
for ins in disasm(target, 80):
    print(f"  {ins.address:08X}: {ins.mnemonic} {ins.op_str}")
    ops = [o.strip() for o in ins.op_str.split(",")]
    if ins.mnemonic == "lis":
        regs[ops[0]] = (int(ops[1], 0) & 0xFFFF) << 16
    elif ins.mnemonic == "ori" and ops[0] == ops[1]:
        regs[ops[0]] = regs.get(ops[0], 0) | (int(ops[2], 0) & 0xFFFF)
    elif ins.mnemonic == "addi" and ops[0] == ops[1]:
        regs[ops[0]] = (regs.get(ops[0], 0) + struct.unpack(">h", struct.pack(">H", int(ops[2], 0) & 0xFFFF))[0]) & 0xFFFFFFFF
    if ins.mnemonic == "blr":
        break
for r in ("r1", "r2", "r13"):
    if r in regs:
        print(f"{r} = 0x{regs[r]:08X}")
