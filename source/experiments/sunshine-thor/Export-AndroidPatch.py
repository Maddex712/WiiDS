"""Export only authored Thor Android host changes, never game sources or assets."""
import subprocess
from pathlib import Path

workspace = Path(__file__).resolve().parents[2]
repo = workspace / "reference/sms-launcher-thor"
expected = "59ef545af26c9ef5026154415d8a5ae5db14c514"
pin = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
if pin != expected:
    raise SystemExit(f"Refusing unexpected Android source pin: {pin}")
files = [
    "android/build.gradle", "android/app/build.gradle",
    "android/app/src/main/AndroidManifest.xml",
    "android/app/src/main/java/dev/chasem/smslauncher/LauncherActivity.java",
    "android/app/src/main/java/dev/chasem/smslauncher/GameActivity.java",
    "android/app/src/main/java/dev/chasem/smslauncher/ControllerProfiles.java",
]
added = [
    "android/app/src/main/java/dev/chasem/smslauncher/ThorStorage.java",
    "android/app/src/main/java/dev/chasem/smslauncher/ThorSessionShutdown.java",
    "android/app/src/main/java/com/wither/sms/ThorLaunchActivity.java",
]
patch = subprocess.check_output(["git", "diff", "--no-ext-diff", "--binary", "HEAD", "--", *files], cwd=repo)
for name in added:
    data = (repo / name).read_bytes()
    if b"\x00" in data:
        raise SystemExit(f"Non-text authored source: {name}")
    lines = data.decode("utf-8").splitlines()
    block = f"diff --git a/{name} b/{name}\nnew file mode 100644\n--- /dev/null\n+++ b/{name}\n@@ -0,0 +1,{len(lines)} @@\n"
    patch += (block + "".join("+" + line + "\n" for line in lines)).encode("utf-8")
target = workspace / "experiments/sunshine-thor/sunshine-thor-android.patch"
target.write_bytes(patch)
print(f"Wrote {len(patch)} source-only bytes to {target}")
