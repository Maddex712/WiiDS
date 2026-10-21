"""Export only enumerated authored sources; exclude all extracted/generated game content."""
from __future__ import annotations

import difflib
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DEST = Path(__file__).resolve().parent
NSMB_PIN = "20f0158dd1d96bd94f821adf80ee361e4fbd414e"
NSMB = [
    "android/app/build.gradle", "android/app/src/main/AndroidManifest.xml",
    *[f"android/app/src/main/java/com/wither/nsmbw/{name}.java" for name in (
        "AppFiles", "AppSettings", "GameActivity", "LauncherActivity", "ThorGameState", "ThorLaunchActivity")],
    "runtime/include/nsmbw_disc_revision.h", "runtime/include/thor_state.h",
    "runtime/src/thor_state.cpp", "runtime/src/android/android_bridge.cpp",
    "runtime/src/guest_profiler.cpp", "runtime/src/hle/vi.cpp",
    "runtime/src/netplay/game_menus.cpp", "runtime/src/system_bridge.cpp",
    "runtime/include/fiber_manager.h", "runtime/src/fiber_manager.cpp",
    "runtime/src/hle/ios.cpp", "runtime/src/main.cpp",
]
MKW = ["android/app/src/main/AndroidManifest.xml",
       "android/app/src/main/kotlin/com/driftdroid/android/ThorGameLaunch.kt"]


def export(repo, revision, paths, filename, verify):
    patch = []
    for path in paths:
        result = subprocess.run(["git", "-C", str(repo), "show", f"{revision}:{path}"], capture_output=True)
        before = result.stdout.decode("utf-8").replace("\r\n", "\n") if result.returncode == 0 else ""
        after = (repo / path).read_text(encoding="utf-8")
        if "\0" in before + after: raise SystemExit(f"Binary content in source allowlist: {path}")
        if before == after: continue
        patch.append(f"diff --git a/{path} b/{path}\n")
        if result.returncode != 0: patch.append("new file mode 100644\n")
        patch.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                     fromfile=f"a/{path}" if result.returncode == 0 else "/dev/null", tofile=f"b/{path}"))
    target = DEST / filename
    target.write_text("".join(patch), encoding="utf-8", newline="\n")
    if verify:
        # Use a temporary index with the source pin. Never stage or reset the real checkout.
        handle, index = tempfile.mkstemp(prefix="thor-nsmb-source-", suffix=".index")
        os.close(handle)
        Path(index).unlink()
        env = dict(os.environ, GIT_INDEX_FILE=index)
        try:
            subprocess.run(["git", "-C", str(repo), "read-tree", revision], env=env, check=True)
            subprocess.run(["git", "-C", str(repo), "apply", "--cached", "--check", str(target)], env=env, check=True)
        finally:
            Path(index).unlink(missing_ok=True)
            Path(index + ".lock").unlink(missing_ok=True)
    print(target)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only-clean-e1", action="store_true", help="Preserve other runtime/launcher patches")
    args = parser.parse_args()
    if not args.only_clean_e1:
        export(ROOT / "reference/nsmbw-android", NSMB_PIN, NSMB, "nsmbw-thor.patch", True)
    e1 = ROOT / "reference/nsmbw-android-e1"
    if e1.is_dir():
        export(e1, NSMB_PIN, NSMB + [
            "projects/nsmbw/recomp.yml", "projects/nsmbw/tools/unpack_game.py",
            "runtime/include/hle/disc_interface.h", "runtime/src/hle/storage/disc_interface.cpp",
            "runtime/src/hle/storage/nand_isfs.cpp", "runtime/src/hle/storage/dvd.cpp",
            "android/app/src/main/java/com/wither/nsmbw/ThorHudAssets.java",
            "android/app/src/main/java/com/wither/nsmbw/ThorLowerScreen.java",
            "android/app/src/main/java/com/wither/nsmbw/ThorWorldMap.java",
            "android/app/src/main/java/com/wither/nsmbw/ThorReserveState.java",
            "runtime/include/thor_world_map.h", "runtime/src/thor_world_map.cpp",
            "runtime/include/thor_reserve.h", "runtime/src/thor_reserve.cpp",
            "runtime/include/abi_bridge.h",
        ], "nsmbw-thor-clean-e1.patch", True)
    if args.only_clean_e1:
        raise SystemExit(0)
    # The MKW manifest already has private Thor changes. Diff against a narrow local baseline
    # containing only the added <queries> block, rather than export that entire private manifest.
    repo = ROOT / "reference/driftdroid"
    path = MKW[0]
    after = (repo / path).read_text(encoding="utf-8")
    added = '\n    <!-- Explicit visibility for the separate NSMB runtime used by the shared Thor library. -->\n    <queries>\n        <package android:name="com.wither.nsmbw.thor" />\n    </queries>\n'
    if after.count(added) != 1: raise SystemExit("Review shared launcher manifest before exporting")
    patch = list(difflib.unified_diff(after.replace(added, "", 1).splitlines(True), after.splitlines(True),
                                    fromfile="a/" + path, tofile="b/" + path))
    picker = "android/app/src/main/kotlin/com/driftdroid/android/ModePickerActivity.kt"
    after_picker = (repo / picker).read_text(encoding="utf-8")
    entry = ('                // Temporary functional entry; Claude owns its eventual case/library presentation.\n'
             '                ThorLauncherView.Entry("New Super Mario Bros. Wii", "Open the native NSMB Wii runtime.") { launchNsmbFromLibrary() },\n')
    method = '''
    private fun launchNsmbFromLibrary() {
        val message = when (ThorGameLaunch.launchNsmb(this)) {
            ThorGameLaunch.Result.OPENED -> return
            ThorGameLaunch.Result.NOT_INSTALLED -> "Install the separate NSMB Wii Thor runtime first."
            ThorGameLaunch.Result.UNAVAILABLE -> "The NSMB Wii runtime could not be opened."
        }
        AlertDialog.Builder(this).setTitle("New Super Mario Bros. Wii")
            .setMessage(message).setPositiveButton(android.R.string.ok, null).show()
    }
'''
    if after_picker.count(entry) != 1 or after_picker.count(method) != 1:
        raise SystemExit("Review the shared launcher action before exporting")
    before_picker = after_picker.replace(entry, "", 1).replace(method, "", 1)
    patch.extend(difflib.unified_diff(before_picker.splitlines(True), after_picker.splitlines(True),
                                    fromfile="a/" + picker, tofile="b/" + picker))
    new = MKW[1]
    patch.extend([f"diff --git a/{new} b/{new}\n", "new file mode 100644\n"])
    patch.extend(difflib.unified_diff([], (repo / new).read_text().splitlines(True), fromfile="/dev/null", tofile="b/" + new))
    target = DEST / "mkw-launch-transport.patch"
    target.write_text("".join(patch), encoding="utf-8", newline="\n")
    print(target)
