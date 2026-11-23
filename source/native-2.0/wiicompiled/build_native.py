from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess

task = Path(__file__).resolve().parent
root = task.parents[1]
sdk = root / ".tools/android-sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64"
out = Path("M:/WiiDS-security-fixes-2026-11-22/native")
output_libraries = out.parent / "android/app/src/main/jniLibs/arm64-v8a"
baseline = Path("M:/WiiDS-retro-mod-startup-2026-11-22/android/app/src/main/jniLibs/arm64-v8a")
common = ["--target=aarch64-none-linux-android29", "--sysroot=" + (sdk / "sysroot").as_posix()]
unity_object = "CMakeFiles/mkw_runtime_common.dir/Unity/unity_runtime_5_cxx.cxx.o"


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(build, log, executable, *args):
    result = subprocess.run([str(sdk / "bin" / executable), *map(str, args)], cwd=build, capture_output=True)
    log.write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"{log}: {result.stderr.decode(errors='replace')[-3000:]}")
    return result.stdout.decode(errors="replace")


def block(rows, target):
    index, row = next((i, row) for i, row in enumerate(rows) if row.startswith(f"build {target}: "))
    source = row.split(": ", 1)[1].split(" ", 1)[1].split(" |", 1)[0]
    fields = {}
    for line in rows[index + 1:]:
        if not line.startswith("  "):
            break
        key, value = line.strip().split(" = ", 1)
        fields[key] = value
    return source, fields


def rebuild(name, build):
    folder = out / name
    folder.mkdir(parents=True, exist_ok=True)
    library = "lib" + name + ".so"
    original_stripped = folder / "baseline.so"
    run(build, folder / "strip-baseline.log", "llvm-strip.exe",
        "--strip-unneeded", "-o", original_stripped, build / library)
    assert sha(original_stripped) == sha(baseline / library), name

    rows = (build / "build.ninja").read_text().splitlines()
    original_unity, flags = block(rows, unity_object)
    original_unity = original_unity.replace("$:", ":")
    unity = Path(original_unity).read_text()
    match = re.search(r'#include "([^"\n]*/nand_fs.cpp)"', unity)
    assert match
    original_source = Path(match[1])
    source = original_source.read_text()
    start = source.index("struct U8Node {")
    end = source.index("// The only FaceLib resource", start)
    source = source[:start] + source[end:]
    source = source.replace("#include <string_view>", '#include <string_view>\n#include "face_lib_u8.h"', 1)
    assert source.count("            ExtractFromU8(arcPath") == 1
    source = source.replace("            ExtractFromU8(arcPath", "            WiiDsFaceLib::ExtractFromU8(arcPath")
    fixed_source = folder / "nand_fs.cpp"
    fixed_source.write_text(source, newline="\n")
    shutil.copyfile(task / "face_lib_u8.h", folder / "face_lib_u8.h")
    fixed_unity = folder / "unity.cxx"
    fixed_unity.write_text(unity.replace(match[1], fixed_source.as_posix()), newline="\n")
    replacement = folder / "runtime.o"
    compile_rsp = folder / "compile.rsp"
    compile_rsp.write_text(" ".join([*common, flags["DEFINES"], flags["FLAGS"], flags["INCLUDES"],
                                    "-I" + original_source.parent.as_posix(), "-c",
                                    fixed_unity.as_posix(), "-o", replacement.as_posix()]))
    run(build, folder / "compile.log", "clang++.exe", "@" + str(compile_rsp))

    inputs, link = block(rows, library)
    objects = inputs.split()
    assert objects.count(unity_object) == 1
    objects[objects.index(unity_object)] = replacement.as_posix()
    link_rsp = folder / "link.rsp"
    link_rsp.write_text(" ".join([*objects, link.get("LINK_PATH", ""), link["LINK_LIBRARIES"]]))
    unstripped = folder / "linked.so"
    run(build, folder / "link.log", "clang++.exe", *common, "-fPIC", *link["LANGUAGE_COMPILE_FLAGS"].split(),
        *link["LINK_FLAGS"].split(), "-shared", "-Wl,-soname," + link["SONAME"],
        "-o", unstripped, "@" + str(link_rsp))
    stripped = folder / library
    run(build, folder / "strip.log", "llvm-strip.exe", "--strip-unneeded", "-o", stripped, unstripped)

    def exports(path, label):
        symbols = run(build, folder / label, "llvm-nm.exe", "-D", "--defined-only", path)
        return {line.split()[-1] for line in symbols.splitlines() if line.strip()}

    assert exports(original_stripped, "baseline-symbols.txt") == exports(stripped, "final-symbols.txt")
    old_dynamic = run(build, folder / "old-dynamic.txt", "llvm-readelf.exe", "-d", original_stripped)
    new_dynamic = run(build, folder / "new-dynamic.txt", "llvm-readelf.exe", "-d", stripped)
    for tag in ("NEEDED", "SONAME"):
        pattern = rf"\({tag}\).*\[(.*)\]"
        assert re.findall(pattern, old_dynamic) == re.findall(pattern, new_dynamic)
    header = run(build, folder / "elf-header.txt", "llvm-readelf.exe", "-h", stripped)
    assert "AArch64" in header
    segments = run(build, folder / "elf-segments.txt", "llvm-readelf.exe", "-lW", stripped)
    assert all(int(line.split()[-1], 16) >= 16384 for line in segments.splitlines()
               if line.lstrip().startswith("LOAD "))
    return library, stripped, {"baseline": sha(original_stripped), "sha256": sha(stripped)}


builds = {
    "WiiCompiled": Path("M:/WiiDS-rr-6.13.4/android-native-drivers"),
    "RetroRewind": Path("M:/WiiDS-rr-updater-2026-11-09/android-native"),
}
with ThreadPoolExecutor(max_workers=2) as pool:
    jobs = [pool.submit(rebuild, name, build) for name, build in builds.items()]
    verified = [job.result() for job in jobs]
report = {}
for name, path, receipt in verified:
    shutil.copyfile(path, output_libraries / name)
    assert sha(output_libraries / name) == receipt["sha256"]
    report[name] = receipt
(task / "native-verification.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
