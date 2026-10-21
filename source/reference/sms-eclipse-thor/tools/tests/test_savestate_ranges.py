#!/usr/bin/env python3
"""tools/savestate_ranges.py: the game's data found in GNU ld and ld64 link maps."""
import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location(
    'ranges', pathlib.Path(__file__).resolve().parents[1] / 'savestate_ranges.py')
ranges = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ranges)

GNU = """\
.data           0x0000000000a00000      0x400
 *(.data .data.* .gnu.linkonce.d.*)
 .data          0x0000000000a00000      0x100 CMakeFiles/sms.dir/platform/ar/ar.cpp.o
 .data          0x0000000000a00100       0x40 libsms_game.a(Application.cpp.o)
                0x0000000000a00100                gpApplication
 *fill*         0x0000000000a00140       0x10
 .data._ZN5TLongNameVeryLongSection
                0x0000000000a00150       0x30 libsms_game.a(Mario.cpp.o)
 .data          0x0000000000a00180       0x20 libsms_gx.a(gx_api.cpp.o)
.data.rel.ro    0x0000000000b00000      0x100
 .data.rel.ro   0x0000000000b00000      0x100 libsms_game.a(Vtables.cpp.o)
.bss            0x0000000000c00000      0x200
 .bss           0x0000000000c00000       0x80 libsms_eclipse.a(bse.cpp.o)
 COMMON         0x0000000000c00080       0x10 libsms_game.a(msl.c.o)
"""

LD64 = """\
# Path: /build/sms
# Arch: x86_64
# Object files:
[  0] linker synthesized
[  1] /build/CMakeFiles/sms.dir/platform/ar/ar.cpp.o
[  2] /build/libsms_game.a(Application.cpp.o)
[  3] /build/libsms_game.a[12](Mario.cpp.o)
# Sections:
# Address\tSize    \tSegment\tSection
0x00001000\t0x00100000\t__TEXT\t__text
0x00200000\t0x00001000\t__DATA\t__data
0x00300000\t0x00001000\t__DATA\t__bss
0x00400000\t0x00001000\t__DATA_CONST\t__const
# Symbols:
# Address\tSize    \tFile  Name
0x00001000\t0x00000010\t[  2] _main
0x00200000\t0x00000040\t[  1] _g_alloc
0x00200040\t0x00000020\t[  2] _gpApplication
0x00200060\t0x00000010\t[  3] _gpMarioOriginal
0x00300000\t0x00000100\t[  3] __ZL6sState
0x00400000\t0x00000080\t[  2] __ZTV12TApplication
# Dead Stripped Symbols:
<<dead>> \t0x00000008\t[  2] _unused
"""


class RangesTest(unittest.TestCase):
    archives = ['libsms_game.a', 'libsms_eclipse.a']

    def test_gnu_ld_keeps_game_data_and_its_padding(self):
        found = ranges.merge(ranges.gnu_ranges(GNU.splitlines(True), self.archives))
        self.assertEqual(found, [[0xa00100, 0xa00180], [0xc00000, 0xc00090]])

    def test_ld64_keeps_game_symbols_in_writable_data(self):
        found = ranges.merge(ranges.ld64_ranges(LD64.splitlines(True), self.archives))
        self.assertEqual(found, [[0x200040, 0x200070], [0x300000, 0x300100]])

    def test_archive_members_in_either_linker_spelling(self):
        self.assertTrue(ranges.from_game('/b/libsms_game.a(X.o)', self.archives))
        self.assertTrue(ranges.from_game('/b/libsms_game.a[3](X.o)', self.archives))
        self.assertFalse(ranges.from_game('/b/libsms_gx.a[3](X.o)', self.archives))
        self.assertFalse(ranges.from_game('/b/CMakeFiles/sms.dir/libsms_game.cpp.o', self.archives))


if __name__ == '__main__':
    unittest.main()
