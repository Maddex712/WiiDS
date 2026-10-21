"""Verify freshly generated canonical E1 translation provenance; not a boot test."""
import argparse
import hashlib
import json
from pathlib import Path

from prepare_clean_e1 import DOL
from prepare_e2 import PIN


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--prepared", required=True, type=Path)
    args = parser.parse_args()
    repo, prepared = args.repo.resolve(), args.prepared.resolve()
    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(prepared / "game/main.dol") != DOL:
        raise SystemExit("Wrong clean E1 executable.")
    metadata = json.loads((repo / "generated/base_translation_output.json").read_text())
    manifest = json.loads((repo / "build/base/nsmbw_base_manifest.json").read_text())
    config = (repo / "generated/RuntimeConfig.h").read_text()
    if metadata["quality"] != {"unsupportedInstructionCount": 0, "invalidSsaFunctionCount": 0}:
        raise SystemExit("Translation quality failure.")
    if manifest["RelBaseAddress"] != 0x807684C0 or "New Super Mario Bros. Wii (USA rev 1)" not in config:
        raise SystemExit("Wrong translation revision/layout.")
    shards = list((repo / "generated/build_shards/base_common").glob("shard_*.cpp"))
    if len(shards) != 72 or len(metadata["functions"]) != 41418:
        raise SystemExit("Unexpected clean E1 function/shard coverage.")
    paths = ["generated/base_translation_output.json", "generated/base_translation_sources.bin",
             "generated/data_sections_init.cpp", "generated/data_sections_init_blobs.S",
             "generated/RuntimeConfig.h", "generated/build_shards/shards.cmake",
             "build/base/nsmbw_base_manifest.json"]
    receipt = dict(status="translation_artifacts_verified_not_gameplay", source_commit=PIN,
                   revision=1, dol_sha256=DOL, translated_metadata_functions=41418,
                   shards=len(shards), quality=metadata["quality"],
                   artifact_sha256={name: sha(repo / name) for name in paths},
                   original_game_bytes_unchanged=True,
                   limits=["Original disc verification remains in translated game code.",
                           "No runtime boot or gameplay result follows from this check."])
    (prepared / "translation-verification.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("Verified canonical E1 translation identity, layout and emitted instruction coverage.")


if __name__ == "__main__":
    main()
