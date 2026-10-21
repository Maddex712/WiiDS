"""Prepare fresh translation inputs from the owner's verified clean USA revision 1.

No game bytes or verification replacements are embedded. The original disc check
is translated unchanged; building these inputs does not establish successful boot.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from prepare_e2 import PIN, flat_image, build_map

DOL = "166eb6b7cca21418e316a6cfbf1bfbf0a213eb8f6396d42126a1482bb764a955"


class IdentityMap:
    def map(self, address):
        return address


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--disc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo, disc, out = (p.resolve() for p in (args.repo, args.disc, args.output))
    if "build" not in out.parts:
        parser.error("Owner-derived outputs must remain in ignored build/.")
    pin = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    if pin != PIN or hashlib.sha256((disc / "sys/main.dol").read_bytes()).hexdigest() != DOL:
        raise SystemExit("Wrong source pin or canonical E1 executable.")
    game = out / "game"
    subprocess.run([sys.executable, str(repo / "projects/nsmbw/tools/unpack_game.py"), str(disc), str(game)], check=True)
    subprocess.run([sys.executable, str(repo / "projects/nsmbw/tools/prelink_rels.py"), str(game)], check=True)
    flat, ranges, layout = flat_image(game)
    seeds = build_map(repo, out, IdentityMap(), flat, ranges, layout)
    mapping = out / "MAP-e1.txt"
    (out / "MAP-e2.txt").replace(mapping)  # Shared discovery helper uses its historical E2 filename.
    project = (repo / "projects/nsmbw/recomp.yml").read_text(encoding="utf-8-sig")
    if DOL not in project:
        raise SystemExit("E1 project must pin the validated clean input before translation.")
    project = re.sub(r"^workspace_root:.*$", "workspace_root: " + repo.as_posix(), project, flags=re.M)
    for old, new in (("../game/main.dol", game / "main.dol"),
                     ("../game/nsmbw_linked.rel", game / "nsmbw_linked.rel"),
                     ("projects/nsmbw/MAP.txt", mapping)):
        project = project.replace("path: " + old, "path: " + new.as_posix())
    (out / "recomp-e1.yml").write_text(project, encoding="utf-8", newline="\n")
    receipt = dict(status="prepared_not_built_or_booted", revision=1, source_commit=PIN,
                   dol_sha256=DOL, original_game_bytes_unchanged=True,
                   input_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in sorted(game.glob("*.dol")) + sorted(game.glob("*.rel"))},
                   seed_discovery=seeds,
                   limits=["No replacement disc-verification bytes supplied.", "Boot still requires a working disc backend and genuine metadata."])
    (out / "preparation.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("Prepared canonical E1 inputs and unchanged original game code for fresh translation.")


if __name__ == "__main__":
    main()
