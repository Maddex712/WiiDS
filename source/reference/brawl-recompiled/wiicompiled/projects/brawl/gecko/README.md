# Gecko code handler

`codehandler.bin` is USB Loader GX's `codehandleronly` (source/patches/codehandleronly.h,
https://github.com/wiidev/usbloadergx, GPL), the Gecko OS code handler that Project+'s Brawl Mod Loader installs at
0x80001800 (entry 0x800018A8). It is translated with the game (tools/build_modules.py adds it to main_translate.dol)
so codes run natively; only code bodies (C0/C2) go through the interpreter.

Changed words (everything else is byte-identical):

| Address    | Original                    | Now                       | Why |
|------------|-----------------------------|---------------------------|-----|
| 0x80001904 | `lis r15, 0x8000`           | `lwz r15, 0x1804(r31)`    | code list address from 0x80001804 (set at install) instead of patched immediates |
| 0x80001908 | `ori r15, r15, 0x22A8`      | `nop`                     | |
| 0x800018E0 | `mtmsr r26`                 | `nop`                     | the runtime owns the MSR |
| 0x800018F4 | `lhz r28, 0x4010(r20)`      | `li r28, 0`               | no memory-interface protection register to unlock |
| 0x800018FC | `sth r21, 0x4010(r20)`      | `nop`                     | |
| 0x80001948 | `sth r28, 0x4010(r20)`      | `nop`                     | |
| 0x80001DB4 | `bl 0x80001E2C`             | `b 0x80002160`            | code types 86/88 pick their operation with a computed jump (the subroutine adds 8*op to lr), which translates as a plain call; 0x80002160..0x800021AC (zero padding before) now compares the op and branches to its table entry |
