"""Stage the lower screen's art for Project+ from its own SD card folder (P+ replaces every portrait and adds
Mewtwo 27, Roy 39 and Knuckles 42).

Usage: python -I stage_pplus_assets.py <Project+ folder> <out dir>
Writes bust_<id>_<costume>.png and stock_<id>_<costume>.png like stage_lower_assets.py. P+ numbers a portrait
<id*10 + costume + 1> inside MenSelchrFaceB<id>0.brres (so a fighter with ten costumes runs into the next
number) and a stock icon InfStc.<id*50 + costume + 1>.
"""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    pplus, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    common = os.path.join(pplus, "pf", "menu", "common")
    files = sorted(glob.glob(os.path.join(common, "char_bust_tex", "MenSelchrFaceB*.brres")))
    files.append(os.path.join(common, "StockFaceTex.brres"))
    count = 0
    with tempfile.TemporaryDirectory() as raw:
        subprocess.run([sys.executable, "-I", os.path.join(HERE, "brres_tex.py"), raw, *files], check=True,
                       stdout=subprocess.DEVNULL)
        for path in glob.glob(os.path.join(raw, "*.png")):
            stem, name = os.path.basename(path)[:-4].split("__")
            bust = re.fullmatch(r"MenSelchrFaceB(\d+)0", stem)
            number = name.split(".")[-1]
            if bust and name.startswith("MenSelchrFaceB.") and number.isdigit():
                fighter = int(bust.group(1))
                costume = int(number) - fighter * 10 - 1
                target = f"bust_{fighter:02d}_{costume}.png"
            elif name.startswith("InfStc.") and len(number) == 4 and number.isdigit() and int(number) % 50:
                fighter, costume = divmod(int(number) - 1, 50)
                target = f"stock_{fighter:02d}_{costume}.png"
            else:
                continue
            if costume < 0:
                continue
            shutil.move(path, os.path.join(out, target))
            count += 1
    print(f"staged {count} images in {out}")


if __name__ == "__main__":
    main()
