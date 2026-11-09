"""Stage the Brawl lower screen's art from the player's own extracted disc (never shipped in the APK).

Usage: python -I stage_lower_assets.py <extracted disc root> <out dir>
Writes <out dir>/bust_<id>_<costume>.png (character-select portraits, 128x160) and stock_<id>_<costume>.png
(stock icons, 32x32), where <id> is the game's two-digit portrait/stock number and <costume> is 0-5.
"""
import glob
import shutil
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    disc, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    files = sorted(glob.glob(os.path.join(disc, "files", "menu", "common", "char_bust_tex", "MenSelchrFaceB*.brres")))
    files.append(os.path.join(disc, "files", "menu", "common", "StockFaceTex_en.brres"))
    with tempfile.TemporaryDirectory() as raw:
        subprocess.run([sys.executable, "-I", os.path.join(HERE, "brres_tex.py"), raw, *files], check=True,
                       stdout=subprocess.DEVNULL)
        count = 0
        for path in glob.glob(os.path.join(raw, "*.png")):
            name = os.path.basename(path)[:-4].split("__")[1]  # MenSelchrFaceB.011 / InfStc.011
            kind, number = name.split(".")
            if len(number) != 3 or not number.isdigit() or number[2] == "0":
                continue
            prefix = "bust" if kind == "MenSelchrFaceB" else "stock" if kind == "InfStc" else None
            if prefix is None:
                continue
            target = os.path.join(out, f"{prefix}_{number[:2]}_{int(number[2]) - 1}.png")
            shutil.move(path, target)
            count += 1
    count += stage_hud(disc, out)
    print(f"staged {count} images in {out}")


def stage_hud(disc, out):
    """The battle HUD's damage digits (stored on their side: turned upright), its %, the series emblems, and the
    title logo, for the 3DS-style panels: dmg_<0-9>.png, dmg_pct.png, mark_<01-20>.png, logo.png."""
    from PIL import Image
    sys.path.insert(0, HERE)
    import compose_case_art

    count = 0
    with tempfile.TemporaryDirectory() as raw:
        subprocess.run([sys.executable, "-I", os.path.join(HERE, "brres_tex.py"), raw,
                        os.path.join(disc, "files", "info", "Info_en.brres"),
                        os.path.join(disc, "files", "menu2", "sc_title_en.pac")], check=True, stdout=subprocess.DEVNULL)
        for d in range(10):
            Image.open(os.path.join(raw, f"Info_en__InfDamageNm.{d}.png")).rotate(90, expand=True).save(
                os.path.join(out, f"dmg_{d}.png"))
            count += 1
        shutil.copy(os.path.join(raw, "Info_en__InfDamageNmPer.png"), os.path.join(out, "dmg_pct.png"))
        for m in range(1, 21):
            shutil.copy(os.path.join(raw, f"Info_en__MenSelchrMark.{m:02d}.png"), os.path.join(out, f"mark_{m:02d}.png"))
        compose_case_art.logo(raw, 900).save(os.path.join(out, "logo.png"))
        count += 22
    return count


if __name__ == "__main__":
    main()
