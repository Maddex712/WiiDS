"""Build NDK shaderc, handling Windows cmd's quoted MRI archive commands."""
from pathlib import Path
import argparse
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--abi',choices=['arm64-v8a','x86_64'],default='arm64-v8a')
args=parser.parse_args()
NDK = ROOT / '.tools/android-sdk/ndk/29.0.14206865'
SOURCE = NDK / 'sources/third_party/shaderc'
OUTPUT = SOURCE / f'libs/c++_static/{args.abi}/libshaderc.a'
LIBRARIES = ['libglslang.a', 'libOGLCompiler.a', 'libOSDependent.a',
             'libshaderc.a', 'libshaderc_util.a', 'libSPIRV.a', 'libHLSL.a',
             'libSPIRV-Tools.a', 'libSPIRV-Tools-opt.a']

if not OUTPUT.is_file():
    result = subprocess.run([str(NDK / 'ndk-build.cmd'), '-C', str(SOURCE),
        'NDK_PROJECT_PATH=.', 'APP_BUILD_SCRIPT=Android.mk', f'APP_ABI={args.abi}',
        'APP_STL=c++_static', 'APP_PLATFORM=android-30', 'libshaderc_combined', '-j8'],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log = ROOT / 'build/galaxy-thor/shaderc-build.log'
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(result.stdout, encoding='utf-8')
    objects = SOURCE / f'obj/local/{args.abi}'
    if any(not (objects / name).is_file() for name in LIBRARIES):
        raise SystemExit(f'Shaderc compile failed; see {log}')
    # Android.mk uses quoted echo commands which cmd.exe writes verbatim.
    # Feed LLVM a real MRI script over stdin, preserving every compiled member.
    archive_tool = NDK / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-ar.exe'
    script = 'create libshaderc_combined.a\n'
    script += ''.join(f'addlib {name}\n' for name in LIBRARIES)
    script += 'save\nend\n'
    subprocess.run([str(archive_tool), '-M'], input=script, text=True,
                   cwd=objects, check=True)
    subprocess.run([str(archive_tool), 't', 'libshaderc_combined.a'],
                   cwd=objects, check=True, stdout=subprocess.DEVNULL)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(objects / 'libshaderc_combined.a', OUTPUT)
print(OUTPUT)
