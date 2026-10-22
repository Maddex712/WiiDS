"""Compile the actual Android input source against mocked events and run on ARM64."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'reference/galaxy-android'
OUT = ROOT / 'build/galaxy-thor/input-tests'
for folder in ('input', 'ui'):
    (OUT / folder).mkdir(parents=True, exist_ok=True)
for name in ('GamepadInput.cpp', 'GamepadInput.h'):
    shutil.copyfile(SOURCE / 'platform/mobile/input' / name, OUT / 'input' / name)
(OUT / 'ui/UIOverlay.h').write_text('''#pragma once
#include <string>
namespace mobile_ui {
struct Settings { float stickSensitivity=1; float starStickSensitivity=1; int rightStickMode=1; };
class UIOverlay { public: static UIOverlay& get(){static UIOverlay value; return value;}
Settings& settings(){return s;} void showToast(const std::string&){} void setPointerPos(float,float){}
bool isSettingsOpen(){return false;} void closeSettings(){} private: Settings s; };
}
''')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
args = parser.parse_args()
compiler = ROOT / '.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android32-clang++.cmd'
exe = OUT / 'test-galaxy-input'
subprocess.run([str(compiler), '-std=c++17', '-O1', '-static-libstdc++',
    '-I' + str(OUT / 'input'), '-I' + str(SOURCE / 'platform/include'),
    str(OUT / 'input/GamepadInput.cpp'), str(Path(__file__).parent / 'tests/gamepad.cpp'),
    '-llog', '-o', str(exe)], check=True)
adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
remote = '/data/local/tmp/test-wiids-galaxy-input'
subprocess.run(adb + ['push', str(exe), remote], check=True)
subprocess.run(adb + ['shell', 'chmod', '700', remote], check=True)
result = subprocess.run(adb + ['shell', remote], check=True, capture_output=True, text=True)
print(result.stdout)
(OUT / 'verification.json').write_text(json.dumps({'sourceSha256': hashlib.sha256(
    (OUT / 'input/GamepadInput.cpp').read_bytes()).hexdigest(), 'result': result.stdout.strip(),
    'scope': 'Actual native input code with mocked events on ARM64; not live gameplay'}, indent=2))
subprocess.run(adb + ['shell', 'rm', remote], check=True)
