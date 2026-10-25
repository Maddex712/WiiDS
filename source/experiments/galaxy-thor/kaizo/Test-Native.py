from pathlib import Path
import json,shlex,subprocess,os
R=Path(__file__).resolve().parents[3];O=R/'build/galaxy-kaizo';S=R/'reference/galaxy-android'
commands=json.loads((R/'build/galaxy-integration/native-x86/compile_commands.json').read_text())
entry=next(e for e in commands if e['file'].endswith('GameEventFlagTable.cpp'))
args=shlex.split(entry['command'],posix=False)
args=[a.strip('"') for a in args]
# Reuse the engine's exact include/ABI options, with native source paths.
args=[a.replace(str(R/'build/galaxy-integration/source').replace('\\','/'),str(S).replace('\\','/')) for a in args]
for flag in ['-o','-c']:
 i=args.index(flag);del args[i:i+2]
args=[a for a in args if not a.startswith(('-MD','-MT','-MF'))]
# Ninja's compile database includes dependency-file arguments after -MF.
args=[a for a in args if not a.endswith('.o.d') and not a.startswith('CMakeFiles/')]
args+=['-UNDEBUG']
objects=[]
for name,path in [('kaizo',S/'platform/src/port/kaizo.cpp'),('flags',S/'decomp/src/Game/System/GameEventFlagTable.cpp'),('test',Path(__file__).with_name('NativeTest.cpp'))]:
 out=O/(name+'-test.o');subprocess.run(args+(['-DPORT_PLATFORM_CODE=1'] if name=='kaizo' else [])+['-ffunction-sections','-fdata-sections','-c',str(path),'-o',str(out)],check=True);objects.append(out)
cc=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/x86_64-linux-android32-clang++.cmd'
subprocess.run([str(cc),'-static-libstdc++','-Wl,--gc-sections',*map(str,objects),'-o',str(O/'native-test')],check=True)
print('Actual-source native test built')
