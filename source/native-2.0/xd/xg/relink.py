from pathlib import Path
import re
import subprocess
import sys

out = Path(__file__).resolve().parent
workbench = Path("C:/Users/super/Documents/ChatGPT/PORTING")
ninja = workbench / "build/xd-mod-2026-11-21/native-build"
previous = Path("M:/WiiDS-combined-preview-2026-11-22/native")
sdk = workbench / ".tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64"
tool = sdk / "bin/clang++.exe"
target = ["--target=aarch64-none-linux-android30", "--sysroot=" + (sdk / "sysroot").as_posix()]
lines = (ninja / "build.ninja").read_text().splitlines()

def block(pattern):
    i, = [idx for idx, line in enumerate(lines) if re.match(pattern, line)]
    left, right = lines[i].removeprefix("build ").split(": ", 1)
    options = {}
    for line in lines[i+1:]:
        if not line.startswith("  "):
            break
        key, value = line.strip().split(" = ", 1)
        options[key] = value
    return left, Path(right.split()[1].replace("$:", ":")), options

def run(command, log):
    with (out / log).open("wb") as result:
        process = subprocess.run([tool, *command], cwd=ninja, stdout=result, stderr=subprocess.STDOUT)
    if process.returncode:
        print((out / log).read_text(errors="replace")[-7000:])
        sys.exit(process.returncode)

rsp = (previous / "xg-link.rsp").read_text()
for local in sys.argv[1:]:
    source = out / local
    if local == "recording.cpp":
        pattern = r"^build third_party/aurora/CMakeFiles/aurora_gx.dir/lib/gfx/recording\.cpp\.o:"
    else:
        pattern = r"^build games/xd/CMakeFiles/xd[.]dir/.*[/]" + re.escape(source.stem) + r"\.cpp\.o:"
    original_object, original_source, options = block(pattern)
    obj = source.with_suffix(".o")
    response = source.with_suffix(".rsp")
    includes = options.get("INCLUDES", "").replace(chr(92), "/")
    if local == "recording.cpp":
        includes += " -iquote " + original_source.parent.as_posix()
    response.write_text(" ".join([*target, options.get("DEFINES", ""), options.get("FLAGS", ""),
                                 includes, "-c", source.as_posix(), "-o", obj.as_posix()]))
    run(["@" + str(response)], source.stem + "-compile.log")
    lookup = original_object
    if local == "recording.cpp":
        lookup = "third_party/aurora/libaurora_gx.a"
        archive = out / "libaurora_gx.a"
        archive.write_bytes((ninja / lookup).read_bytes())
        member = out / Path(original_object).name
        member.write_bytes(obj.read_bytes())
        archiver = sdk / "bin/llvm-ar.exe"
        members = subprocess.check_output([archiver, "t", archive], text=True).splitlines()
        assert members.count(member.name) == 1
        subprocess.run([archiver, "r", archive, member], check=True, cwd=ninja)
        assert subprocess.check_output([archiver, "t", archive], text=True).splitlines() == members
        obj = archive
    assert lookup in rsp, lookup
    rsp = rsp.replace(lookup, obj.as_posix())
response = out / "xg-link.rsp"
response.write_text(rsp)
_, _, options = block(r"^build games/xd/libxd_xg\.so:")
run([*target, "-fPIC", *options["LANGUAGE_COMPILE_FLAGS"].split(), *options["LINK_FLAGS"].split(),
     "-shared", "-Wl,-soname,libxd_xg.so", "-o", (out / "libxd_xg.so").as_posix(),
     "@" + str(response)], "link.log")
