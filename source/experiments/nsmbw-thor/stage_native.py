"""Stage a verified, explicitly selected local ARM64 build for the private Thor APK."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PIN = "20f0158dd1d96bd94f821adf80ee361e4fbd414e"
DOLS = {
    1: "166eb6b7cca21418e316a6cfbf1bfbf0a213eb8f6396d42126a1482bb764a955",
    2: "0373b4dbffd7c9da9d27eb7257a7677877a3e33ceb81818bab1daa93560b9907",
}
SYSTEM_LIBS = {"libandroid.so", "liblog.so", "libdl.so", "libc.so", "libm.so", "libz.so",
               "libvulkan.so", "libEGL.so", "libGLESv1_CM.so", "libGLESv2.so", "libOpenSLES.so", "libaaudio.so"}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def validate_receipts(receipt, translation, revision, dol):
    if receipt.get("status") != "built" or receipt.get("disc_revision") != revision:
        raise SystemExit(f"A completed E{revision} native build receipt is required.")
    if receipt.get("source_commit") != PIN or receipt.get("dol_sha256") != dol:
        raise SystemExit("Native build has the wrong source/disc inputs.")
    if translation.get("source_commit") != PIN or translation.get("revision") != revision or translation.get("dol_sha256") != dol:
        raise SystemExit(f"No matching E{revision} translation receipt.")
    if translation.get("status") != "translation_artifacts_verified_not_gameplay":
        raise SystemExit("Translation artifact verification must pass before staging.")


def validate_cache(cache, revision):
    flags = re.search(r"(?m)^CMAKE_CXX_FLAGS:STRING=(.*)$", cache)
    if not flags or "MKW_BUILD_PRODUCTS:BOOL=ON" not in cache:
        raise SystemExit("Build cache does not select a native game product.")
    has_e2 = re.search(r"(?:^|\s)-DNSMBW_DISC_REVISION_E2(?:=(\S+))?(?=\s|$)", flags[1])
    if revision == 2 and (not has_e2 or has_e2[1] != "1"):
        raise SystemExit("Build cache lacks the E2 compile flag.")
    if revision == 1 and has_e2:
        raise SystemExit("E1 native staging refuses any E2 compile definition.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", type=int, choices=(1, 2), default=2)
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--prepared", type=Path)
    # A mod build (MSMBWii) is translated from a patched copy of this revision's DOL; its
    # preparation receipt names the patched hash, which is then required here instead.
    parser.add_argument("--dol-sha256")
    args = parser.parse_args()
    revision = args.revision
    dol = args.dol_sha256 or DOLS[revision]
    # E2 defaults remain unchanged; E1 stays in its separate checkout/build tree.
    repo = (args.repo or ROOT / ("reference/nsmbw-android-e1" if revision == 1 else "reference/nsmbw-android")).resolve()
    build = (args.build or ROOT / ("build/nsmbw-thor/e1/android-native" if revision == 1 else "build/nsmbw-thor/android-native")).resolve()
    prepared = (args.prepared or ROOT / ("build/nsmbw-thor/e1/prepared" if revision == 1 else "build/nsmbw-thor/e2-prepared")).resolve()
    native = repo / "android/native"
    library = build / "libmain.so"
    receipt_file = build / "native-build-receipt.json"
    receipt = load(receipt_file)
    translation_file = prepared / "translation-verification.json"
    translation = load(translation_file)
    validate_receipts(receipt, translation, revision, dol)
    actual_pin = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    if actual_pin != PIN:
        raise SystemExit("Runtime checkout is not at the pinned source commit.")
    if digest(prepared / "game/main.dol") != dol:
        raise SystemExit("Prepared DOL differs from the compiled disc revision.")
    if translation.get("status") != "translation_artifacts_verified_not_gameplay":
        raise SystemExit("Translation artifact verification must pass before staging.")
    for relative, expected in translation["artifact_sha256"].items():
        if digest(repo / relative) != expected:
            raise SystemExit(f"Generated translation changed after verification: {relative}")
    if digest(library) != receipt.get("library_sha256"):
        raise SystemExit("Native output changed after its build receipt.")
    with library.open("rb") as stream:
        header = stream.read(20)
    if header[:5] != b"\x7fELF\x02" or struct.unpack_from("<H", header, 18)[0] != 183:
        raise SystemExit("Native output is not ELF64 AArch64.")
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    validate_cache(cache, revision)
    newest_source = max(p.stat().st_mtime for folder in (repo / "runtime/src", repo / "runtime/include", repo / "generated")
                        for p in folder.rglob("*") if p.suffix in {".h", ".hpp", ".cpp", ".S", ".bin"})
    if library.stat().st_mtime < newest_source:
        raise SystemExit("Native library predates a runtime or generated input; rebuild first.")
    tools = ROOT / ".tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin"
    strip = tools / "llvm-strip.exe"
    readelf = tools / "llvm-readelf.exe"
    required = {"libmain.so": library}
    pending = [library]
    while pending:
        source = pending.pop()
        dynamic = subprocess.check_output([str(readelf), "-d", str(source)], text=True)
        for name in re.findall(r"Shared library: \[([^\]]+)\]", dynamic):
            if name in SYSTEM_LIBS or name in required:
                continue
            candidates = list(build.rglob(name))
            if len(candidates) != 1:
                raise SystemExit(f"Need exactly one built dependency for {name}, found {len(candidates)}")
            required[name] = candidates[0]
            pending.append(candidates[0])
    runtime_files = {name: build / name for name in ("dsp_coef.bin", "cacert.pem", "initial_pipeline_cache.db")}
    for source in runtime_files.values():
        if not source.is_file(): raise SystemExit(f"Missing runtime asset {source}")
    bootstrap = build / "wii_bootstrap"
    if not bootstrap.is_dir(): raise SystemExit("Missing runtime NAND bootstrap")
    libdir, assets = native / "jniLibs/arm64-v8a", native / "assets/runtime"
    libdir.mkdir(parents=True, exist_ok=True)
    assets.mkdir(parents=True, exist_ok=True)
    stale = [p.name for p in libdir.glob("*.so") if p.name not in required]
    if stale:
        raise SystemExit(f"Unrelated old staged libraries need inspection: {stale}")
    for name, source in required.items():
        subprocess.run([str(strip), "--strip-unneeded", "-o", str(libdir / name), str(source)], check=True)
    for name, source in runtime_files.items():
        shutil.copyfile(source, assets / name)
    shutil.copytree(bootstrap, assets / "wii_bootstrap", dirs_exist_ok=True)
    staged = dict(schema=1, disc_id="SMNE01", disc_revision=revision, dol_sha256=dol,
                  source_commit=PIN, library_sha256=digest(libdir / "libmain.so"),
                  libraries={name: digest(libdir / name) for name in sorted(required)},
                  native_build_receipt_sha256=digest(receipt_file),
                  translation_verification_sha256=digest(translation_file),
                  limits=["Staged locally. No boot or gameplay validation is implied."])
    (native / "thor-native.json").write_text(json.dumps(staged, indent=2) + "\n")
    print(f"Staged {len(required)} native libraries and runtime assets for Thor USA revision {revision}.")
    print(native / "thor-native.json")


if __name__ == "__main__":
    main()
