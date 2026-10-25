from pathlib import Path
import json,shlex,subprocess
R=Path(__file__).resolve().parents[3];O=R/'build/galaxy-kaizo';S=R/'reference/galaxy-android'
db=json.loads((R/'build/galaxy-integration/native-x86/compile_commands.json').read_text())
entry=next(e for e in db if e['file'].endswith('nand.cpp'))
args=[a.strip('"') for a in shlex.split(entry['command'],posix=False)]
args=[a.replace(str(R/'build/galaxy-integration/source').replace('\\','/'),str(S).replace('\\','/')) for a in args]
for flag in ['-o','-c']:
 i=args.index(flag);del args[i:i+2]
args=[a for a in args if not a.startswith(('-MD','-MT','-MF')) and not a.endswith('.o.d') and not a.startswith('CMakeFiles/')]
args+=['-UNDEBUG','-ffunction-sections','-fdata-sections']
objects=[]
for name,path in [('profile',S/'platform/src/port/profile.cpp'),('nand',S/'platform/src/nand/nand.cpp'),('profile-test',Path(__file__).with_name('ProfileTest.cpp'))]:
 out=O/(name+'-check.o');subprocess.run(args+['-c',str(path),'-o',str(out)],check=True);objects.append(out)
cc=R/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/x86_64-linux-android32-clang++.cmd'
subprocess.run([str(cc),'-static-libstdc++','-Wl,--gc-sections',*map(str,objects),'-o',str(O/'profile-test')],check=True)
print('Actual-source profile/NAND test built')
