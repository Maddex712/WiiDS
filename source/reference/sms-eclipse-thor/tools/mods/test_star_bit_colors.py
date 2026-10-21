#!/usr/bin/env python3
"""Check the fetched Star Bit constructor and colour accessor on both widths.

Usage: python3 tools/mods/test_star_bit_colors.py BUILD/eclipse-src/eclipse
The table read is instrumented to reject an invalid index before reading it.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from test_stage_transition import function


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('eclipse', type=Path)
    parser.add_argument('--compiler', default='clang++')
    args = parser.parse_args()
    source = (args.eclipse / 'src/object/star_bit.cpp').read_text(encoding='utf-8-sig')
    header = (args.eclipse / 'include/object/star_bit.hxx').read_text()
    constructor = function(source, 'TStarBit::TStarBit(const char *name)')
    load = function(source, 'void TStarBit::load(JSUMemoryInputStream &in)')
    accessor = function(header, 'static JUtility::TColor getColorFromIndex(int index)')
    assert accessor.count('sStarBitColorArray[index]') == 1
    accessor = accessor.replace('sStarBitColorArray[index]', 'checkedColor(index)')
    palette = re.search(r'const JUtility::TColor TStarBit::sStarBitColorArray\[6\]\{.*?\};',
                        source, re.S).group()
    code = r'''
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstring>
#include <initializer_list>
namespace JUtility { struct TColor { unsigned char r, g, b, a; }; }
int Clamp(int x, int low, int high) { return x < low ? low : x > high ? high : x; }
int randomValue;
int testRand() { return randomValue; }
#define rand testRand
struct JSUMemoryInputStream {
    const unsigned char *data;
    unsigned offset = 0;
    void read(void *out, unsigned size) {
        std::memcpy(out, data + offset, size);
        offset += size;
    }
    void readBE(void *out, unsigned size) {
        assert(size == 4);
        const unsigned char *p = data + offset;
        unsigned word = (unsigned(p[0]) << 24) | (unsigned(p[1]) << 16)
                        | (unsigned(p[2]) << 8) | unsigned(p[3]);
        std::memcpy(out, &word, 4);
        offset += 4;
    }
};
struct TCoin {
    TCoin(const char *) {}
    void load(JSUMemoryInputStream &) {}
};
struct TStarBit : TCoin {
    static const JUtility::TColor sStarBitColorArray[6];
    bool mIsMoving, mIsBouncing, mIsAttracting;
    float mBounceSpeedY;
    void *mGlowEffect;
    float mGlowSize, mGlowAnmSpeed;
    int mGlowTick;
    JUtility::TColor mColor;
    TStarBit(const char *);
    void load(JSUMemoryInputStream &);
    static JUtility::TColor checkedColor(int index) {
        assert(index >= 0 && index < 6);
        return sStarBitColorArray[index];
    }
''' + accessor + r'''
};
unsigned s_particle_init_tick;
''' + palette + '\n' + constructor + '\n' + load + r'''
bool equal(JUtility::TColor a, JUtility::TColor b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
int main() {
    unsigned counts[6] = {};
    for (int seed = 0; seed < 48; ++seed) {
        randomValue = seed;
        TStarBit bit("test");
        assert(equal(bit.mColor, TStarBit::sStarBitColorArray[seed % 6]));
        ++counts[seed % 6];
    }
    for (int i = 0; i < 6; ++i) {
        assert(counts[i] == 8);
        assert(equal(TStarBit::getColorFromIndex(i), TStarBit::sStarBitColorArray[i]));
    }
    for (int i : {INT_MIN, -1})
        assert(equal(TStarBit::getColorFromIndex(i), TStarBit::sStarBitColorArray[0]));
    for (int i : {6, 23, 24, INT_MAX})
        assert(equal(TStarBit::getColorFromIndex(i), TStarBit::sStarBitColorArray[5]));
    for (int index : {0, 1, 2, 3, 4, 5, -1, 6}) {
        for (unsigned char moving : {static_cast<unsigned char>(0), static_cast<unsigned char>(1)}) {
            unsigned word = static_cast<unsigned>(index);
            unsigned char data[] = {moving, static_cast<unsigned char>(word >> 24),
                static_cast<unsigned char>(word >> 16), static_cast<unsigned char>(word >> 8),
                static_cast<unsigned char>(word)};
            randomValue = 2;
            TStarBit bit("test");
            bit.mIsBouncing = false;
            JSUMemoryInputStream stream{data};
            bit.load(stream);
            assert(stream.offset == sizeof(data));
            assert(bit.mIsMoving == bool(moving) && bit.mIsBouncing == bool(moving));
            assert(equal(bit.mColor, TStarBit::sStarBitColorArray[index >= 0 && index < 6 ? index : 2]));
        }
    }
}
'''
    with tempfile.TemporaryDirectory(prefix='sms-star-colours-') as folder:
        path = Path(folder) / 'test.cpp'
        path.write_text(code)
        for arch in ('32', '64'):
            binary = Path(folder) / ('test' + arch)
            subprocess.run([args.compiler, '-std=c++17', '-m' + arch, '-O2', str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print('star-bit: %s-bit random colours, index boundaries and scene byte order passed' % arch)


if __name__ == '__main__':
    main()
