from pathlib import Path
import concurrent.futures
import hashlib
import json
import re
import shutil
import subprocess
import sys

task = Path(__file__).resolve().parent
root = task.parents[1]
base = root/"build/xd-mod-2026-11-21"
ninja = base/"native-build"
out = Path("M:/WiiDS-combined-preview-2026-11-22/native")
output_libs = out.parent/"android/app/src/main/jniLibs/arm64-v8a"
out.mkdir(exist_ok=True)
sdk = root/".tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64"
clang = sdk/"bin/clang++.exe"
common = ["--target=aarch64-none-linux-android30","--sysroot="+(sdk/"sysroot").as_posix()]
feedback = Path("M:/WiiDS-feedback-fixes-2026-11-22/xd-native")

def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream,"sha256").hexdigest()

def run(args,label):
    result=subprocess.run(list(map(str,args)),cwd=ninja,capture_output=True)
    (task/(label+".log")).write_bytes(result.stdout+result.stderr)
    if result.returncode:
        raise RuntimeError((result.stderr+result.stdout).decode(errors="replace")[-7000:])
    return result.stdout.decode(errors="replace")

def block(pattern):
    lines=(ninja/"build.ninja").read_text().splitlines()
    matches=[(i,line) for i,line in enumerate(lines) if re.match(pattern,line)]
    assert len(matches)==1,pattern
    i,line=matches[0]
    target, rule_and_inputs=line.removeprefix("build ").split(": ",1)
    inputs=rule_and_inputs.split(" ",1)[1].split(" |",1)[0]
    values={}
    for inner in lines[i+1:]:
        if not inner.startswith("  "):break
        key, value=inner.strip().split(" = ",1)
        values[key]=value
    return target,inputs,values

def compile_new(pattern,name,edit):
    old,raw,flags=block(pattern)
    old_source=Path(raw.replace("$:",":"))
    text=edit(old_source.read_text())
    src=out/(name+".cpp")
    src.write_text(text,newline="\n")
    obj=out/(name+".o")
    rsp=out/(name+".rsp")
    includes = flags.get("INCLUDES", "").replace("\\", "/")
    rsp.write_text(" ".join([*common,flags.get("DEFINES",""),flags.get("FLAGS",""),includes,
                             "-c",src.as_posix(),"-o",obj.as_posix()]))
    run([clang,"@"+str(rsp)],name)
    return old,obj

def runtime(s):
    def replace(old,new,count=1):
        nonlocal s
        assert s.count(old)==count,old
        s=s.replace(old,new)
    replace("std::array<std::atomic<int>,12>","constexpr int kRuntimeValues=13;\nstd::array<std::atomic<int>,kRuntimeValues>")
    replace('"gameplay.text_speed","gameplay.fast_battle_animations"', '"gameplay.text_speed","gameplay.fast_battle_animations","gameplay.fast_forward"')
    replace("i<12","i<kRuntimeValues",3)
    replace("count!=12","(count!=12&&count!=kRuntimeValues)")
    replace("for(int i=0;i<kRuntimeValues;i++)out[i]","for(int i=0;i<count;i++)out[i]")
    replace("key>=12","key>=kRuntimeValues")
    return s

def move_learning(s):
    for old,new in [
        ("u32 name = (u32)GSmsgGetGSchar(wazaDB_GetName(OboeWazaNo));","const GCuptr name = (GCuptr)GSmsgGetGSchar(wazaDB_GetName(OboeWazaNo));"),
        ("u32 nick = (u32)fightPokemonGetNicknamePtr(fp);","const GCuptr nick = (GCuptr)fightPokemonGetNicknamePtr(fp);"),
    ]:
        assert s.count(old)==1,old
        s=s.replace(old,new)
    return s

def host():
    old=base/"host-selector/host.cpp"
    s=old.read_text()
    for before,after in [
        ("jint values[12]={};if(!fn||!fn(values,12))return nullptr;",
         "constexpr int count=13;\n    jint values[count]={};if(!fn||!fn(values,count))return nullptr;"),
        ("env->NewIntArray(12)","env->NewIntArray(count)"),
        ("SetIntArrayRegion(result,0,12,values)","SetIntArrayRegion(result,0,count,values)"),
    ]:
        assert s.count(before)==1,before
        s=s.replace(before,after)
    src=out/"host.cpp"
    src.write_text(s,newline="\n")
    obj=out/"host.o"
    rsp=out/"host.rsp"
    template=(base/"host-selector/host-host.rsp").read_text()
    template=template.replace(old.as_posix(),src.as_posix()).replace((base/"host-selector/host-host.o").as_posix(),obj.as_posix())
    assert src.as_posix() in template and obj.as_posix() in template
    rsp.write_text(template)
    run([clang,"@"+str(rsp)],"host-compile")
    jni=root/"build/xd-thor/pass16-dualscreen-2026-11-15/native/host-xd_jni.o"
    run([clang,*common,"-shared","-Wl,-z,max-page-size=16384","-Wl,--no-undefined","-Wl,-soname,libwiids_xd_host.so",
         "-o",out/"libwiids_xd_host.so",obj,jni,output_libs/"libSDL3.so",output_libs/"libwiids_xd_memory.so",
         "-landroid","-llog","-ldl"],"host-link")

def restore_android_dsp_mapping(text):
    accepted = (root/"build/xd-thor/pass12-jni-2026-11-15/native-v6/gc_dsp_sdk.cpp").read_text()
    at = accepted.index("struct NativeSpan {")
    end = accepted.index("void dsp_log(", at)
    mapping = accepted[at:end]
    first = text.index("uint8_t* dsp_memory(")
    last = text.index("void dsp_log(", first)
    text = text[:first] + mapping + text[last:]
    text = text.replace("#include <chrono>", "#include <chrono>\n#include <link.h>\n#include <android/log.h>")
    text = text.replace("void PortOS_RaiseInterrupt(", "int PortLow_Owns(const void*);\nvoid PortOS_RaiseInterrupt(",1)
    anchor = "  if (sDsp != nullptr) return;\n  const GcDspHost host = make_host();"
    assert text.count(anchor)==1
    return text.replace(anchor, "  if (sDsp != nullptr) return;\n  register_image_spans();\n  const GcDspHost host = make_host();")


def keep_android_particle_buffers_coherent(text):
    for old, android in (
        ("        u8* p = mem + 0x40000000;", "        u8* p = mem;"),
        ("        bufA = mem + 0x40000000;", "        bufA = mem;"),
    ):
        assert text.count(old) == 1
        text = text.replace(old, "#ifdef __ANDROID__\n" + android + "\n#else\n" + old + "\n#endif")
    old = "            bufA = (u8*)((u32)bufA - 0x40000000);"
    assert text.count(old) == 1
    return text.replace(old, "#ifndef __ANDROID__\n" + old + "\n#endif")


def end_android_particle_primitives(text):
    include = re.search(r'^#include "([^"\n]*/stripped/game/maximum/GSAPI/particle/psdisp.cpp)"$', text, re.M)
    assert include
    original = Path(include[1]).read_text()
    fixed = (root / "build/xd-thor/pass12-jni-2026-11-15/native-v9/psdisp-stripped.cpp").read_text()
    boundary = r"^ *GXEnd\(\); // Aurora requires an explicit primitive boundary[.]\n"
    assert len(re.findall(boundary, fixed, re.M)) == 8
    assert re.sub(boundary, "", fixed, flags=re.M) == original
    source = out / "xg-psdisp-boundaries.cpp"
    source.write_text(fixed, newline="\n")
    return text.replace(include[1], source.as_posix())


def skip_xg_intro_logos(text):
    original = '              false, false)\n          .OriginalNote("both logo movies play to their end.")'
    assert text.count(original) == 1
    # Only the archive linked into Android's XG library receives this default.
    return text.replace(original, original.replace("false, false", "true, false"))


def replace_archive_member(link_libraries, archive_path, member_name, replacement, log_prefix):
    new_archive = out / Path(archive_path).name
    shutil.copyfile(ninja / archive_path, new_archive)
    new_member = out / member_name
    shutil.copyfile(replacement, new_member)
    archiver = sdk / "bin/llvm-ar.exe"
    before = run([archiver, "t", new_archive], log_prefix + "-members-before").splitlines()
    assert before.count(member_name) == 1
    run([archiver, "r", new_archive, new_member], log_prefix + "-archive")
    assert run([archiver, "t", new_archive], log_prefix + "-members-after").splitlines() == before
    assert archive_path in link_libraries
    return link_libraries.replace(archive_path, new_archive.as_posix())


with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    objects=[
        pool.submit(compile_new,r"^build .*fightSeqSpAction[.]cpp[.]o:","xg-move-learning",move_learning),
        pool.submit(compile_new,r"^build .*xd_runtime_controls[.]cpp[.]o:","xg-runtime-controls",runtime),
        pool.submit(compile_new, r"^build .*peContainer[.]cpp[.]o:", "xg-particle-buffer", keep_android_particle_buffers_coherent),
        pool.submit(compile_new, r"^build .*psdisp[.]cpp[.]o:", "xg-particle-primitives", end_android_particle_primitives),
    ]
    host_result=pool.submit(host)
    changes=[future.result() for future in objects]
    host_result.result()

_,inputs,link=block(r"^build games/xd/libxd_xg[.]so:")
items=inputs.split()
assert len(items)>500
for before,after in changes:
    assert items.count(before)==1,before
    items[items.index(before)]=after.as_posix()
_,dsp=compile_new(r"^build .*gc_dsp_sdk[.]cpp[.]o:","xg-dsp",restore_android_dsp_mapping)
_, settings = compile_new(r"^build .*standard_settings[.]cpp[.]o:", "xg-settings", skip_xg_intro_logos)
link_libraries = replace_archive_member(link["LINK_LIBRARIES"], "platform/libplatform_audio.a",
                                       "gc_dsp_sdk.cpp.o", dsp, "audio")
link_libraries = replace_archive_member(link_libraries, "engine/libengine_settings.a",
                                       "standard_settings.cpp.o", settings, "settings")
rsp=out/"xg-link.rsp"
rsp.write_text(" ".join([*items,link.get("LINK_PATH",""),link_libraries]))
run([clang,*common,"-fPIC",*link["LANGUAGE_COMPILE_FLAGS"].split(),*link["LINK_FLAGS"].split(),
     "-shared","-Wl,-soname,libxd_xg.so","-o",out/"libxd_xg.so","@"+str(rsp)],"xg-link")
lib_dirs=base/"native-build/games/xd"

def elf(kind,path):
    return run([sdk/"bin/llvm-readelf.exe",kind,path],path.parent.name+"-"+path.name+"-"+kind.removeprefix("-"))

def needed(path):
    return set(re.findall(r"\(NEEDED\).*\[(.+?)\]",elf("-d",path)))

def symbols(path):
    return {row.split()[-1] for row in run([sdk/"bin/llvm-nm.exe","-D","--defined-only",path],
            path.parent.name+"-"+path.name+"-symbols").splitlines() if row.strip()}

baseline = {"libwiids_xd_host.so":base/"host-selector/libwiids_xd_host.so",
            "libxd_xg.so":lib_dirs/"libxd_xg.so"}
for name,older in baseline.items():
    new=out/name
    assert "AArch64" in elf("-h",new)
    loads=[line for line in elf("-lW",new).splitlines() if line.lstrip().startswith("LOAD ")]
    assert loads and all(int(line.split()[-1],16)>=16384 for line in loads),loads
    assert needed(new)==needed(older)
    assert symbols(older)<=symbols(new)
    shutil.copyfile(new,output_libs/name)
assert sha(output_libs/"libxd.so")==sha(feedback/"libxd.so")
receipt={name:sha(output_libs/name) for name in ("libxd.so","libxd_xg.so","libwiids_xd_host.so")}
(task/"native-verification.json").write_text(json.dumps(receipt,indent=2)+"\n")
print(json.dumps(receipt,indent=2))
