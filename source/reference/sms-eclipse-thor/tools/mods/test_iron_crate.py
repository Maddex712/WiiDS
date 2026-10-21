#!/usr/bin/env python3
"""Exercise Eclipse's fixed-up iron-crate model setup with bounded storage.

Usage: python3 tools/mods/test_iron_crate.py BUILD/eclipse-src/eclipse
Checks both host widths and both lighting/model-loader flag branches without
a ROM. The original one-slot keeper fails when the broken model is registered.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

from test_stage_transition import function


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('eclipse', type=Path)
    parser.add_argument('--compiler', default='clang++')
    args = parser.parse_args()
    setup = function((args.eclipse / 'src/object/iron_crate.cpp').read_text(encoding='utf-8-sig'),
                     'void TIronCrate::makeMActors()')
    code = r'''
#include <cassert>
#include <cstring>
using u32 = unsigned int;
struct MActor {
    const char *name;
    int light = -1, _40 = 1, calculations = 0, views = 0;
    void setLightID(int id) { light = id; }
    void calc() { ++calculations; }
    void viewCalc() { ++views; }
};
struct TMActorKeeper {
    unsigned capacity, count = 0;
    u32 mModelFlags = 0;
    MActor **actors;
    TMActorKeeper(void *, unsigned n) : capacity(n), actors(new MActor*[n]) {}
    ~TMActorKeeper() {
        for (unsigned i = 0; i < count; ++i) delete actors[i];
        delete[] actors;
    }
    MActor *createMActor(const char *name, u32 flags) {
        // The game writes these slots without a bound check. Fail at that
        // boundary so this regression detects the overflow on either host.
        assert(count < capacity);
        assert(flags == 0x1234);
        return actors[count++] = new MActor{name};
    }
};
struct TIronCrate {
    void *mLiveManager = nullptr;
    TMActorKeeper *mActorKeeper = nullptr;
    u32 mModelLoadFlags;
    MActor *mActorData = nullptr, *m_model_datas[2] = {};
    bool rootCalculated = false;
    TIronCrate(u32 flags) : mModelLoadFlags(flags) {}
    ~TIronCrate() { delete mActorKeeper; }
    u32 getSDLModelFlag() { return 0x1234; }
    void calcRootMatrix() { assert(mActorData); rootCalculated = true; }
    void makeMActors();
};
''' + setup + r'''
int main() {
    for (u32 flags : {0u, 0x4000u, 0x8000u, 0xc000u}) {
        TIronCrate crate(flags);
        crate.makeMActors();
        assert(crate.mActorKeeper->count == 2);
        assert(crate.mActorKeeper->mModelFlags == ((flags & 0x8000) ? 0x112F0000u : 0x102F0000u));
        assert(crate.mActorData == crate.m_model_datas[0]);
        assert(crate.m_model_datas[0] != crate.m_model_datas[1]);
        assert(std::strcmp(crate.m_model_datas[0]->name, "rocketbox.bmd") == 0);
        assert(std::strcmp(crate.m_model_datas[1]->name, "rocketbox_crash.bmd") == 0);
        assert(crate.rootCalculated);
        for (auto *model : crate.m_model_datas) {
            assert(model->calculations == 1 && model->views == 1);
            assert(model->light == ((flags & 0x4000) ? 0 : -1));
            assert(model->_40 == ((flags & 0x4000) ? 0 : 1));
        }
    }
}
'''
    with tempfile.TemporaryDirectory(prefix='sms-iron-crate-') as folder:
        source = Path(folder) / 'test.cpp'
        source.write_text('#include <initializer_list>\n' + code)
        for arch in ('32', '64'):
            binary = Path(folder) / ('test' + arch)
            subprocess.run([args.compiler, '-std=c++17', '-m' + arch, '-O2', str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print('iron-crate: %s-bit intact/broken models and lighting flags passed' % arch)


if __name__ == '__main__':
    main()
