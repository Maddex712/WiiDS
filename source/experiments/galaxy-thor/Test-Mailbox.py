"""Exercise the actual DSP queue on ARM64; the unpatched source must fail."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[2]
source = root / 'reference/galaxy-android'
out = root / 'build/galaxy-thor/mailbox-tests'
out.mkdir(parents=True, exist_ok=True)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
args = parser.parse_args()
compiler = root / '.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang++.cmd'
adb = [str(root / '.tools/platform-tools/adb.exe'), '-s', args.serial]
original = (source / 'platform/src/audio/dsp_mailbox.cpp').read_text()
results = []
for fixed in (False, True):
    test_source = original if fixed else original.replace('    PortHostAllocScope hostAlloc;', '')
    (out / 'dsp_mailbox.cpp').write_text(test_source)
    exe = out / ('mailbox-fixed' if fixed else 'mailbox-baseline')
    subprocess.run([str(compiler), '-std=c++17', '-O1', '-static-libstdc++', '-DTARGET_PC',
        '-I' + str(out), '-I' + str(source / 'platform/include'),
        '-I' + str(source / 'decomp/libs/RVL_SDK/include'),
        str(Path(__file__).parent / 'tests/dsp-mailbox.cpp'), '-o', str(exe)], check=True)
    remote = '/data/local/tmp/test-wiids-galaxy-mailbox'
    subprocess.run(adb + ['push', str(exe), remote], check=True, capture_output=True)
    subprocess.run(adb + ['shell', 'chmod', '700', remote], check=True)
    result = subprocess.run(adb + ['shell', remote], capture_output=True, text=True)
    assert result.returncode == (0 if fixed else 1), result.stdout + result.stderr
    results.append({'fixed': fixed, 'exitCode': result.returncode, 'output': result.stdout.strip()})
    print(results[-1])
subprocess.run(adb + ['shell', 'rm', remote], check=True)
(out / 'verification.json').write_text(json.dumps({'result': 'PASS', 'cases': results,
    'sourceSha256': hashlib.sha256(original.encode()).hexdigest(),
    'scope': 'Actual DSP mailbox with instrumented allocation routing; baseline fails, fix passes'}, indent=2))
