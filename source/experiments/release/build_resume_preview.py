"""Build resume guards with the dev7 achievement baseline, without editing the paused workbench.

Reuse the existing native dependency graph. Recompile the two NSMB unity units
that the paused Casual-client experiment touched, from copies with its added
source omitted and its exact saved dev7 VI source restored. Link without its
archive. All other units are the normal native build, including the copy guard.
"""
from pathlib import Path
import json
import hashlib
import shutil
import subprocess
import argparse

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--diagnostic', action='store_true', help='Retain native symbols and compile Android fault-PC diagnostics for the actual Thor crash')
parser.add_argument('--overlay-guard', action='store_true', help='Build the real ImGui lifetime correction over the dev7 achievement profile')
args = parser.parse_args()
OUT = ROOT / ('build/feature-1.1/thor-polish-2026-10-24/native-fixed' if args.overlay_guard else ('build/feature-1.1/thor-polish-2026-10-24/native-probe' if args.diagnostic else 'build/feature-1.1/resume/dev7-native'))
OUT.mkdir(parents=True, exist_ok=True)
BUILD = ROOT / 'build/nsmbw-thor/e1/android-native'
NINJA = ROOT / '.tools/host-tools/bin/ninja.exe'
BIN = ROOT / '.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    delta = json.loads((ROOT/'build/ra-casual/source-delta.json').read_text())['files']
    # Refuse to silently mask later independent work in the paused experiment.
    relevant = [e for e in delta if e['path'].startswith('reference/nsmbw-android-e1/')]
    assert len(relevant) == 5
    for entry in relevant: assert sha(ROOT/entry['path']) == entry['after_sha256'], entry['path']
    old_vi = ROOT/'build/ra-casual/before/source/reference/nsmbw-android-e1/runtime/src/hle/vi.cpp'
    vi_entry = next(e for e in relevant if e['path'].endswith('/vi.cpp'))
    assert sha(old_vi) == vi_entry['before_sha256']
    graph = json.loads(subprocess.check_output([str(NINJA),'-C',str(BUILD),'-t','compdb','-x'],text=True))
    replacements = {}
    commands = []
    names = ['unity_runtime_2_cxx.cxx','unity_runtime_7_cxx.cxx']
    if args.diagnostic or args.overlay_guard: names.append('unity_runtime_6_cxx.cxx')
    if args.overlay_guard: names.append('unity_runtime_4_cxx.cxx')
    for name in names:
        original = BUILD/'CMakeFiles/mkw_runtime_common.dir/Unity'/name
        text = original.read_text()
        if name == 'unity_runtime_2_cxx.cxx':
            ra = f'#include "{(ROOT/"reference/nsmbw-android-e1/runtime/src/thor_ra.cpp").as_posix()}"'
            assert text.count(ra) == 1
            text = text.replace(ra,'// Paused Casual client omitted; matches dev7 runtime sources.')
        elif name == 'unity_runtime_7_cxx.cxx':
            vi = (ROOT/'reference/nsmbw-android-e1/runtime/src/hle/vi.cpp').as_posix()
            assert text.count(vi) == 1
            text = text.replace(vi,old_vi.as_posix())
        copied = OUT/name; copied.write_text(text,encoding='utf-8')
        entry = next(x for x in graph if x['output'].endswith('/'+name+'.o'))
        obj = OUT/(name+'.o')
        command = entry['command'].replace(original.as_posix(),copied.as_posix())
        assert command != entry['command']
        command = command.replace(entry['output']+'.d',obj.as_posix()+'.d').replace(entry['output'],obj.as_posix())
        commands.append(command); replacements[entry['output']] = obj.as_posix()
    with (OUT/'compile.log').open('w',encoding='utf-8') as log:
        for n,command in enumerate(commands):
            run = subprocess.run(command,cwd=BUILD,stdout=log,stderr=subprocess.STDOUT)
            assert run.returncode == 0, f'Baseline unit {n} failed; inspect compile.log'
    link = next(x['command'] for x in graph if x['output'] == 'libnsmbw.so')
    # CMake wraps the link with unrelated mkdir/copy commands. Execute only clang.
    pieces = link.split(' && ')
    link = next(x for x in pieces if 'clang++.exe ' in x and ' -o libnsmbw.so' in x)
    assert link.count('wiids_achievements/libwiids_rcheevos.a') == 1
    link = link.replace('wiids_achievements/libwiids_rcheevos.a','')
    for old,new in replacements.items():
        assert old in link; link = link.replace(old,new)
    output = OUT/'libnsmbw.so'
    link = link.replace(' -o libnsmbw.so ',f' -o {output.as_posix()} ')
    assert ' -o libnsmbw.so ' not in link and 'rcheevos' not in link
    with (OUT/'link.log').open('w',encoding='utf-8') as log:
        run = subprocess.run(link,cwd=BUILD,stdout=log,stderr=subprocess.STDOUT)
        assert run.returncode == 0, 'Link failed; inspect link.log'
    if args.diagnostic or args.overlay_guard: shutil.copy2(output, OUT/'libnsmbw.unstripped.so')
    subprocess.run([str(BIN/'llvm-strip.exe'),'--strip-unneeded',str(output)],check=True)
    symbols = subprocess.check_output([str(BIN/'llvm-nm.exe'),'-D','--defined-only',str(output)],text=True)
    assert all(s not in symbols for s in ['RetroAchievementsSession','rc_client_','ThorRa','thor_ra_'])
    assert 'Java_com_wither_nsmbw_GameActivity_' in symbols
    (OUT/'symbols.txt').write_text(symbols,encoding='utf-8')
    (OUT/'commands.txt').write_text('\n'.join(commands+[link])+'\n',encoding='utf-8')
    (OUT/'verification.json').write_text(json.dumps({
        'status':'PASS','libnsmbw_sha256':sha(output),'bytes':output.stat().st_size,
        'dev7_vi_source_sha256':sha(old_vi),'paused_client_code_omitted':True,
        'resume_guard_source_sha256':sha(ROOT/'reference/nsmbw-android-e1/runtime/src/hle/gx/gx_copy.cpp'),
        'overlay_frame_guard':args.overlay_guard,
        'overlay_source_sha256':sha(ROOT/'reference/nsmbw-android-e1/runtime/src/settings_overlay.cpp'),
        'scope':'Actual ARM64 compile/link against existing native dependencies; no device/gameplay test',
        'workbench_paused_sources_unchanged':True,
    },indent=2)+'\n',encoding='utf-8')
    print('PASS native NSMB resume build with dev7 VI and no Casual client symbols')

if __name__ == '__main__': main()
