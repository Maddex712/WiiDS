from pathlib import Path
import hashlib
import os
import re
import subprocess
import sys

task = Path(__file__).resolve().parent
root = task.parents[1]
port = task / "upstream"
old = root / "build/xd-thor/pass4-2026-11-14"


def resolve(rel, replacements):
    path = port / rel
    data = path.read_text(encoding="utf-8")
    conflicts = re.compile(r"^<<<<<<< [^\n]*\n(.*?)^=======\n(.*?)^>>>>>>> [^\n]*\n", re.M | re.S)
    choices = iter(replacements)
    data, count = conflicts.subn(lambda m: next(choices)(m[1], m[2]), data)
    assert count == len(replacements), (rel, count)
    path.write_text(data, encoding="utf-8")


def patch(rel, old, new):
    path = port / rel
    data = path.read_text(encoding="utf-8")
    assert data.count(old) == 1, (rel, old)
    path.write_text(data.replace(old, new), encoding="utf-8")


def prepare():
    resolve("CMakeLists.txt", [lambda upstream, wiids: upstream + wiids])
    resolve("games/xd/CMakeLists.txt", [
        lambda upstream, wiids: "if (NOT ANDROID)\n" + upstream,
        lambda upstream, wiids: upstream.removesuffix("if (MSVC)\n")
        + "if (ANDROID)\n  set(XD_PROGRAM_LINK_OPTIONS -Wl,-Bsymbolic-functions -Wl,-z,max-page-size=16384)\nelseif (MSVC)\n",
    ])
    resolve("platform/CMakeLists.txt", [lambda upstream, wiids: upstream.replace("OR APPLE)", "OR APPLE OR ANDROID)")])
    resolve("games/xd/src/boot.c", [lambda upstream, wiids: wiids])
    resolve("games/xd/src/settings_boot.cpp", [lambda upstream, wiids: upstream + wiids])
    resolve("snapshot/f109416dd7/src/game/menuNameEntry.cpp", [lambda upstream, wiids: upstream + wiids])
    boot = port / "games/xd/src/boot.c"
    data = boot.read_text(encoding="utf-8")
    data = data.replace("#ifdef __linux__\nstatic void linux_heap_low_enough", "#if defined(__linux__) && !defined(__ANDROID__)\nstatic void linux_heap_low_enough")
    data = data.replace("#ifdef __linux__\n  linux_heap_low_enough", "#if defined(__linux__) && !defined(__ANDROID__)\n  linux_heap_low_enough")
    boot.write_text(data, encoding="utf-8")
    patch("games/xd/CMakeLists.txt", "add_library(xd SHARED ${XD_PROGRAM_PORT_SOURCES} $<TARGET_OBJECTS:xd_game> $<TARGET_OBJECTS:xd_musyx>)",
          'add_library(xd SHARED ${XD_PROGRAM_PORT_SOURCES} $<TARGET_OBJECTS:xd_game> $<TARGET_OBJECTS:xd_musyx>)\nset_target_properties(xd PROPERTIES OUTPUT_NAME xd_xg)')
    # The WiiDS Aurora tree already carries direct renderer and Android adaptations.
    # Bind the stamp check to the exact prior patch set while reconciling upstream additions below.
    old_patch_dir = old / "port/third_party/patches/aurora"
    patch("CMakeLists.txt", "${PROJECT_SOURCE_DIR}/third_party/patches/aurora/*.patch", old_patch_dir.as_posix() + "/*.patch")
    receipt = port / "third_party/aurora/.port_patches_applied"
    assert receipt.read_text() == "".join(f"{p.name} {hashlib.sha1(p.read_bytes()).hexdigest()}\n" for p in sorted(old_patch_dir.glob("*.patch")))


def run(build):
    vs = Path("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake")
    cmake = vs / "CMake/bin/cmake.exe"
    ndk = root / ".tools/android-sdk/ndk/29.0.14206865"
    output = task / "native-build"
    old_tools = root / "build/xd-thor/runtime-2026-11-14"
    env = os.environ.copy()
    env.update(RUSTUP_HOME=str(old_tools / "rustup"), CARGO_HOME=str(old_tools / "cargo"),
               PYTHONPATH=str(old_tools / "python-deps"),
               CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER=str(ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android30-clang.cmd"))
    env["PATH"] = str(old_tools / "cargo/bin") + os.pathsep + env["PATH"]
    if build:
        args = [cmake, "--build", output, "--target", "xd", "--parallel", "16", "--", "-k", "0"]
    else:
        args = [cmake, "-S", port, "-B", output, "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + str(vs / "Ninja/ninja.exe"),
                "-DCMAKE_TOOLCHAIN_FILE=" + str(ndk / "build/cmake/android.toolchain.cmake"), "-DANDROID_ABI=arm64-v8a",
                "-DANDROID_PLATFORM=android-30", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF",
                "-DAURORA_DAWN_PROVIDER=package", "-DAURORA_NOD_PROVIDER=vendor",
                "-DRust_CARGO_TARGET=aarch64-linux-android",
                "-DPython3_EXECUTABLE=C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe"]
        for dep in (old / "cmake/_deps").glob("*-src"):
            args.append("-DFETCHCONTENT_SOURCE_DIR_" + dep.name.removesuffix("-src").upper() + "=" + dep.as_posix())
    log = task / ("native-build.log" if build else "native-configure.log")
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(args, env=env, stdout=stream, stderr=subprocess.STDOUT)
    print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-30:]))
    return result.returncode


if __name__ == "__main__":
    if "--prepare" in sys.argv:
        prepare()
    raise SystemExit(run("--build" in sys.argv))
