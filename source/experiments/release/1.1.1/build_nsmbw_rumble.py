"""NSMB/MSMBWii rumble: WPADControlMotor_HLE (runtime/src/hle/input/wpad.cpp, unity unit 0) now
drives the player's controller motor. Reuses the Discord round's accepted link command
(M:/WiiDS-discord-bugfix-2026-10-26/native/<variant>/plan.json final_link), first proving it still
reproduces the shipped library byte for byte, then swaps in only a freshly compiled unit 0. Never
runs CMake or a ninja build; the existing object graph and PCH are inputs only."""
from pathlib import Path
import hashlib, json, subprocess, sys

ROOT = Path(__file__).resolve().parents[2]
NINJA = ROOT / '.tools/host-tools/bin/ninja.exe'
TOOLS = ROOT / '.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'
DISCORD = Path('M:/WiiDS-discord-bugfix-2026-10-26/native')
SHIPPED = Path('M:/WiiDS-rr-6.13.4/android/app/src/main/jniLibs/arm64-v8a')
OUT = Path('M:/WiiDS-rr-6.13.4/nsmbw-rumble')
LIBS = {'e1': 'libnsmbw.so', 'msmbwii': 'libmsmbwii.so'}
UNIT = 'CMakeFiles/mkw_runtime_common.dir/Unity/unity_runtime_0_cxx.cxx.o'


def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def run(command, cwd, log):
    with log.open('w', encoding='utf-8') as f:
        r = subprocess.run(command, cwd=cwd, stdout=f, stderr=subprocess.STDOUT)
    assert r.returncode == 0, f'{log}: exit {r.returncode}'


def strip(src, dst):
    subprocess.run([str(TOOLS / 'llvm-strip.exe'), '--strip-unneeded', '-o', str(dst), str(src)], check=True)


result = {}
for variant, lib in LIBS.items():
    build = ROOT / f'build/nsmbw-thor/{variant}/android-native'
    out = OUT / variant
    out.mkdir(parents=True, exist_ok=True)
    plan = json.loads((DISCORD / variant / 'verification.json').read_text())
    link = plan['final_link']
    discord_out = (DISCORD / variant / lib.replace('.so', '.unstripped.so')).as_posix()
    assert link.count(f' -o {discord_out} ') == 1

    # 1. The accepted link must still give the shipped library.
    baseline = out / ('baseline.' + lib)
    run(link.replace(f' -o {discord_out} ', f' -o {baseline.as_posix()} '), build, out / 'baseline-link.log')
    strip(baseline, out / ('baseline-stripped.' + lib))
    shipped = sha(SHIPPED / lib)
    assert sha(out / ('baseline-stripped.' + lib)) == shipped, f'{variant}: baseline does not reproduce the shipped library'
    baseline.unlink()

    # 2. Unit 0 again, from the edited wpad.cpp, into this folder only.
    graph = json.loads(subprocess.check_output([str(NINJA), '-C', str(build), '-t', 'compdb', '-x'], text=True))
    entry = next(e for e in graph if e['output'] == UNIT)
    obj = out / 'unity_runtime_0_cxx.cxx.o'
    command = entry['command'].replace(UNIT.replace('/', '\\') + '.d', obj.as_posix() + '.d')
    command = command.replace(UNIT + '.d', obj.as_posix() + '.d').replace(UNIT, obj.as_posix())
    assert UNIT not in command and obj.as_posix() + '.d' in command
    run(command, build, out / 'compile-unit-0.log')
    deps = (out / 'unity_runtime_0_cxx.cxx.o.d').read_text()
    assert 'hle/input/wpad.cpp' in deps.replace('\\', '/')

    # 3. Relink with only unit 0 replaced.
    assert link.count(' ' + UNIT + ' ') == 1, f'{variant}: unit 0 is not linked from the graph'
    target = out / lib.replace('.so', '.unstripped.so')
    final = link.replace(' ' + UNIT + ' ', ' ' + obj.as_posix() + ' ').replace(f' -o {discord_out} ', f' -o {target.as_posix()} ')
    run(final, build, out / 'link.log')
    strip(target, out / lib)
    symbols = subprocess.check_output([str(TOOLS / 'llvm-nm.exe'), '-D', '--defined-only', str(out / lib)], text=True)
    assert 'Java_com_wither_nsmbw_GameActivity_' in symbols
    assert not any(s in symbols for s in ['RetroAchievementsSession', 'rc_client_', 'ThorRa', 'thor_ra_'])
    target.unlink()
    result[variant] = {'lib': str(out / lib), 'sha256': sha(out / lib), 'shipped_sha256': shipped,
                       'baseline_reproduced': True, 'recompiled': ['unity_runtime_0 (hle/input/wpad.cpp)']}
    print(variant, 'PASS', result[variant]['sha256'], flush=True)

(OUT / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
