#!/usr/bin/env python3
"""Compile BSE's fixed-up scene commit and optional-parameter loader in isolation.

Usage: python3 tools/mods/test_stage_transition.py BUILD/eclipse-src/bse
Pass --compiler with the clang++ path used to build Eclipse if needed.
Checks both host widths, including a deferred unlock movie -> custom menu ->
stage transition with current episode 0xFF and a valid pending destination.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bse', type=Path)
    parser.add_argument('--compiler', default='clang++')
    args = parser.parse_args()
    application = (args.bse / 'src/application.cpp').read_text()
    start = application.index('// This fixes the secret area movies')
    end = application.index('app->mContext = delayContext;', start)
    commit = application[start:end]
    load = function((args.bse / 'src/stage.cpp').read_text(),
                    'void BetterSMS::Stage::TStageParams::load(const char *stageName)')
    code = r'''
#include <cassert>
#include <cstring>
using s32 = int;
struct Scene { unsigned char mAreaID, mEpisodeID; unsigned short flags; };
struct TApplication {
    enum { CONTEXT_DIRECT_STAGE = 5, CONTEXT_DIRECT_LEVEL_SELECT = 9 };
    unsigned char mContext;
    Scene mPrevScene, mCurrentScene, mNextScene;
};
void commitScene(TApplication *app, bool sIsAdditionalMovie, unsigned char delayContext) {
''' + commit + r'''
}
int entry = -1, lookups = 0, resets = 0, loads = 0;
struct DVDFileInfo { int mLen = 4; };
int DVDConvertPathToEntrynum(const char *) { ++lookups; return entry; }
void DVDFastOpen(int, DVDFileInfo *) {}
void DVDReadPrio(DVDFileInfo *, void *, int, int, int) {}
void DVDClose(DVDFileInfo *) {}
struct JKRHeap {
    static void *alloc(int, int, void *) { static char data[4]; return data; }
    static void free(void *, void *) {}
};
struct JSUMemoryInputStream { JSUMemoryInputStream(void *, int) {} };
struct TParams { void load(JSUMemoryInputStream &) { ++loads; } };
namespace BetterSMS { namespace Stage {
struct TStageParams : TParams {
    bool mIsCustomConfigLoaded = true;
    void reset() { ++resets; mIsCustomConfigLoaded = false; }
    void load(const char *);
    void stageNameToParamPath(char *path, const char *name, bool) {
        assert(name); std::strcpy(path, name);
    }
};
}}
''' + load + r'''
int main() {
    // Deferred movie: current still names a selection placeholder. The custom
    // menu must publish the concrete destination before stage setup uses it.
    TApplication app{11, {5,4,7}, {1,255,0}, {5,0,3}};
    commitScene(&app, true, 5);
    assert(app.mCurrentScene.mAreaID == 5 && app.mCurrentScene.mEpisodeID == 0);
    assert(app.mCurrentScene.flags == 3);
    assert(app.mPrevScene.mAreaID == 5 && app.mPrevScene.mEpisodeID == 4);
    // A real prior episode is retained when changing areas.
    app = {11, {0,0,0}, {1,2,9}, {5,0,3}};
    commitScene(&app, true, 5);
    assert(app.mPrevScene.mAreaID == 1 && app.mPrevScene.mEpisodeID == 2);
    // Additional movies still preserve the current scene; custom menus that
    // return to another context must also leave it alone.
    for (unsigned char context : {static_cast<unsigned char>(5), static_cast<unsigned char>(6), static_cast<unsigned char>(11)}) {
        app = {context, {5,4,7}, {1,255,0}, {5,0,3}};
        commitScene(&app, true, context == 11 ? 8 : 5);
        assert(app.mCurrentScene.mAreaID == 1 && app.mCurrentScene.mEpisodeID == 255);
    }
    app = {5, {0,0,0}, {1,2,9}, {5,0,3}};
    commitScene(&app, false, 5);
    assert(app.mCurrentScene.mAreaID == 5 && app.mPrevScene.mEpisodeID == 2);
    app = {11, {1,2,9}, {5,0,3}, {5,0,3}};
    commitScene(&app, false, 5);
    assert(app.mPrevScene.mAreaID == 1 && app.mPrevScene.mEpisodeID == 2);
    // Null archive name resets stale custom settings without any DVD lookup.
    BetterSMS::Stage::TStageParams params;
    params.load(nullptr);
    assert(resets == 1 && lookups == 0 && loads == 0 && !params.mIsCustomConfigLoaded);
    params.load("missing.szs");
    assert(resets == 2 && lookups == 2 && !params.mIsCustomConfigLoaded);
    entry = 0;
    params.load("pinnaBeach0.szs");
    assert(lookups == 3 && loads == 1 && params.mIsCustomConfigLoaded);
}
'''
    code = '#include <initializer_list>\n' + code
    with tempfile.TemporaryDirectory(prefix='sms-stage-transition-') as directory:
        source = Path(directory) / 'check.cpp'
        binary = Path(directory) / 'check'
        source.write_text(code)
        for arch in (32, 64):
            subprocess.run([args.compiler, '-std=c++17', '-O2', f'-m{arch}',
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print(f'PASS: {arch}-bit deferred scene transition and optional stage parameters')


if __name__ == '__main__':
    main()
