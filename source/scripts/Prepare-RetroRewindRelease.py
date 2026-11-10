"""Prepare verified Retro Rewind translation inputs and launcher pins in a new directory."""

import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path, PurePosixPath


PACK = "RetroRewind6"
CODE = f"{PACK}/Binaries/Code.pul"
VERSION_FILE = f"{PACK}/version.txt"
LOADER = f"{PACK}/xml/RetroRewind6.xml"
EXTERNAL_LOADER = "riivolution/RetroRewind6.xml"
CDN = "https://cdn.update.rwfc.net/RetroRewind/zip"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inspect(path, version, full):
    with zipfile.ZipFile(path) as archive:
        files = [entry.filename for entry in archive.infolist() if not entry.is_dir()]
        if len({name.casefold() for name in files}) != len(files):
            raise ValueError(f"Duplicate member in {path.name}")
        if archive.read(VERSION_FILE).decode().strip() != version:
            raise ValueError(f"{path.name} does not contain Retro Rewind {version}")
        selected = {}
        for name in files:
            if name not in (VERSION_FILE, EXTERNAL_LOADER) and not name.startswith(
                (f"{PACK}/Binaries/", f"{PACK}/xml/")
            ):
                continue
            parts = name.split("/")
            if any(part in ("", ".", "..") or re.search(r"[:\\\x00-\x1f]", part) for part in parts):
                raise ValueError(f"Unsafe member name in {path.name}")
            entry = archive.getinfo(name)
            if entry.file_size > 32 * 1024 * 1024:
                raise ValueError(f"Unexpected translation input size for {name}")
            selected[name] = archive.read(entry)
        if full and (CODE not in selected or LOADER not in selected):
            raise ValueError("Full archive is missing translation inputs")
        return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="Exact version in the official Retro Rewind release list")
    parser.add_argument("full_archive", type=Path, help="Full ZIP downloaded from the official CDN")
    parser.add_argument("delta_archive", type=Path, help="Matching official update ZIP")
    parser.add_argument("output", type=Path, help="New private directory for translation inputs")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){1,3}", args.version):
        parser.error("Expected a numeric Retro Rewind version")
    if args.full_archive.resolve() == args.delta_archive.resolve():
        parser.error("Provide separate full and delta archives")

    full = inspect(args.full_archive, args.version, full=True)
    delta = inspect(args.delta_archive, args.version, full=False)
    for name, data in delta.items():
        # The update can omit the binaries or loader when they did not change.
        if name != VERSION_FILE and name in full and full[name] != data:
            raise ValueError(f"The archives disagree on {name}")
    if EXTERNAL_LOADER in delta and delta[EXTERNAL_LOADER] != full[LOADER]:
        raise ValueError("The delta loader differs from the full archive's translation loader")
    if EXTERNAL_LOADER in full and full[EXTERNAL_LOADER] != full[LOADER]:
        raise ValueError("The full archive contains conflicting loaders")
    with args.full_archive.open("rb") as stream:
        archive_hash = hashlib.file_digest(stream, "sha256").hexdigest()
    receipt = {
        "version": args.version,
        "archive_url": f"{CDN}/{args.version}-full.zip",
        "archive_bytes": args.full_archive.stat().st_size,
        "archive_sha256": archive_hash,
        "code_pul_sha256": digest(full[CODE]),
        "loader_xml_sha256": digest(full[LOADER]),
        "delta_contains_code": CODE in delta,
    }
    args.output.mkdir()
    for name, data in full.items():
        if not name.startswith(f"{PACK}/"):
            continue
        path = args.output.joinpath(*PurePosixPath(name).parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    report = json.dumps(receipt, indent=2) + "\n"
    (args.output / "release-inputs.json").write_text(report, encoding="utf-8")
    print(report, end="")


if __name__ == "__main__":
    main()
