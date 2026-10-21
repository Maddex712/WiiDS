#!/usr/bin/env python3
"""Check BSE's fetched settings-label ownership and all three update paths.

Usage: python3 tools/mods/test_settings_text.py BUILD/eclipse-src/bse
Runs on both host widths and checks independent text ownership, synchronized
shadows, and cleanup without leaked initial strings or double deletion.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bse', type=Path)
    parser.add_argument('--compiler', default='clang++')
    args = parser.parse_args()
    setup = (args.bse / 'src/settings.cpp').read_text()
    updates = (args.bse / 'src/p_settings.hxx').read_text()
    front = re.search(r'(?:delete\[\] settingText->mStrPtr;\s*)?'
                      r'settingText->mStrPtr\s*= settingTextBuf;', setup).group()
    back = re.search(r'(?:delete\[\] settingTextBehind->mStrPtr;\s*)?'
                     r'settingTextBehind->mStrPtr\s*= [^;]+;'
                     r'(?:\s*memcpy\(settingTextBehind->mStrPtr, settingTextBuf, 100\);)?', setup).group()
    update_paths = re.findall(
        r'snprintf\(mCurrentSettingInfo->mSettingTextBox->mStrPtr, 100, "%s: %s",\s*'
        r'mCurrentSettingInfo->mSettingData->getName\(\), valueTextBuf\);'
        r'(?:\s*memcpy\(mCurrentSettingInfo->mSettingTextBoxBack->mStrPtr,\s*'
        r'mCurrentSettingInfo->mSettingTextBox->mStrPtr, 100\);)?', updates)
    assert len(update_paths) == 3, 'expected refresh, left, and right update paths'
    # Track the actual assignment/allocation operations. Reject invalid frees
    # at the boundary rather than invoking undefined behaviour in the test.
    setup_code = (front + '\n' + back).replace('new char[100]', 'owned(100)')
    setup_code = re.sub(r'delete\[\] ([^;]+);', r'release(\1);', setup_code)
    code = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
std::set<char *> allocations;
char *owned(unsigned size) {
    char *p = new char[size]{};
    allocations.insert(p);
    return p;
}
void release(char *p) {
    assert(allocations.erase(p) == 1);
    delete[] p;
}
struct Box {
    char *mStrPtr = owned(1);
    ~Box() { release(mStrPtr); }
};
struct Setting { const char *getName() { return "Test"; } };
struct Info { Box *mSettingTextBox, *mSettingTextBoxBack; Setting *mSettingData; };
void setup(Box *settingText, Box *settingTextBehind) {
    char *settingTextBuf = owned(100);
    std::snprintf(settingTextBuf, 100, "Test: OFF");
''' + setup_code + '\n}\n'
    for i, body in enumerate(update_paths):
        code += 'void update%d(Info *mCurrentSettingInfo, const char *valueTextBuf) {\n%s\n}\n' % (i, body)
    code += r'''
int main() {
    {
        Box front, back;
        setup(&front, &back);
        assert(front.mStrPtr != back.mStrPtr);
        assert(allocations.size() == 2);
        assert(std::strcmp(front.mStrPtr, "Test: OFF") == 0);
        assert(std::strcmp(front.mStrPtr, back.mStrPtr) == 0);
        Setting setting;
        Info info{&front, &back, &setting};
        void (*paths[])(Info *, const char *) = {update0, update1, update2};
        for (auto update : paths) {
            update(&info, "ON");
            assert(std::strcmp(front.mStrPtr, "Test: ON") == 0);
            assert(std::strcmp(front.mStrPtr, back.mStrPtr) == 0);
            update(&info, "OFF");
            assert(std::strcmp(front.mStrPtr, "Test: OFF") == 0);
            assert(std::strcmp(front.mStrPtr, back.mStrPtr) == 0);
        }
    }
    assert(allocations.empty());
}
'''
    with tempfile.TemporaryDirectory(prefix='sms-settings-text-') as folder:
        source = Path(folder) / 'test.cpp'
        source.write_text(code)
        for arch in ('32', '64'):
            binary = Path(folder) / ('test' + arch)
            subprocess.run([args.compiler, '-std=c++17', '-m' + arch, '-O2', str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print('settings-text: %s-bit ownership, updates and cleanup passed' % arch)


if __name__ == '__main__':
    main()
