"""Capture only the foreground NSMB Thor session, with scoped live-state logs."""
import argparse
from pathlib import Path
import subprocess
from concurrent.futures import ThreadPoolExecutor
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--handheld", action="store_true", help="Store the NSMB2-style feature evidence separately")
    args = parser.parse_args()
    if not args.name or not all(c.isalnum() or c in "-_" for c in args.name):
        parser.error("name must contain only letters, digits, hyphen or underscore")
    adb = [str(ROOT / ".tools/platform-tools/adb.exe"), "-s", args.serial]
    focus = subprocess.check_output(adb + ["shell", "dumpsys", "activity", "activities"], text=True)
    if not any("topResumedActivity" in line and "com.wither.nsmbw.thor/com.wither.nsmbw.GameActivity" in line
               for line in focus.splitlines()):
        raise SystemExit("NSMB game is not foreground; no capture made")
    pid = subprocess.check_output(adb + ["shell", "pidof", "com.wither.nsmbw.thor:game"], text=True).strip()
    log = subprocess.check_output(adb + ["logcat", "-d", "--pid=" + pid, "-s", "NSMBW-ThorHUD:I", "AndroidRuntime:E"], text=True)
    if "Lower screen attached" not in log:
        raise SystemExit("No lower-display attachment in this game process; no capture made")
    out = ROOT / ("build/nsmbw-thor/e1/handheld" if args.handheld else "build/nsmbw-thor/e1/second-screen")
    out.mkdir(parents=True, exist_ok=True)
    (out / (args.name + "-state.log")).write_text(log, encoding="utf-8")
    def capture(panel, display):
        target = out / (args.name + "-" + panel + ".png")
        with target.open("wb") as stream:
            subprocess.run(adb + ["exec-out", "screencap", "-d", display, "-p"], stdout=stream, check=True)
        with Image.open(target) as image:
            image.thumbnail((960, 640))
            image.save(target.with_stem(target.stem + "-preview"))
        print(target)
    with ThreadPoolExecutor(max_workers=2) as pool:
        captures = [pool.submit(capture, panel, display) for panel, display in
                    (("upper", "4630946441858561667"), ("lower", "4630946482288158084"))]
        for capture_result in captures:
            capture_result.result()


if __name__ == "__main__":
    main()
