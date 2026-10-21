"""Verify and stage owner data for an explicitly selected NSMB Thor disc revision."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import tarfile
import uuid

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = "com.wither.nsmbw.thor"
EXPECTED_BY_REVISION = {2: {
    "sys/main.dol": "0373b4dbffd7c9da9d27eb7257a7677877a3e33ceb81818bab1daa93560b9907",
    "files/rels/d_profileNP.rel.LZ": "51bccc66ff299411f46adac5520e28d5b3e890bd93713acee697f89b673d4011",
    "files/rels/d_basesNP.rel.LZ": "eb58ce05b3664be786752f1ced1dd1e11b2aa86216e56719e46402d18338db95",
    "files/rels/d_enemiesNP.rel.LZ": "f7941b87baf0b6151f087bcc41323aa43877f1c9f1763b327bfd2869e8a28955",
    "files/rels/d_en_bossNP.rel.LZ": "a0946cd4f7a1d461ba4f316e3db63f619917246b2050e13660759e5e5a8d3875",
}, 1: {
    "sys/main.dol": "166eb6b7cca21418e316a6cfbf1bfbf0a213eb8f6396d42126a1482bb764a955",
    "files/rels/d_profileNP.rel.LZ": "f04ca9636e20feb5bdacdb8a1115a2b4a71e99f5091df56b4099ae8ea36c7eaf",
    "files/rels/d_basesNP.rel.LZ": "66b51efac566aead05e4b2351f64800ca54cd38aa93d8d1c9961a054f2854ebb",
    "files/rels/d_enemiesNP.rel.LZ": "9896eab47a57ee0d3e2644393dd4c35e81171662da020fdb6bebc9307a4a9283",
    "files/rels/d_en_bossNP.rel.LZ": "ca28f19ed9fa1a16d5bca7260e9e0127ecd0a964697531da16747d8edffac124",
}}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verified_bca_source(source):
    """Accept only an explicitly supplied existing 64-byte file; never supply contents."""
    if source is None:
        return None
    try:
        path = source.resolve(strict=True)
    except OSError as failure:
        raise SystemExit(f"Cannot read supplied BCA file: {failure}")
    if not path.is_file() or path.stat().st_size != 64:
        raise SystemExit("The supplied BCA source must be an existing regular file of exactly 64 bytes.")
    return path


def write_archive(disc, bca, manifest, archive):
    files = sorted(p for folder in (disc / "files", disc / "sys") for p in folder.rglob("*") if p.is_file())
    members = []
    for path in files:
        relative = path.relative_to(disc).as_posix()
        if path.is_symlink() or any(c in relative for c in "\r\n\\"):
            raise SystemExit(f"Unsafe extraction path: {relative!r}")
        members.append((path, relative))
    if bca is not None:
        # This is the caller's actual supplied file, even when it lives outside the extracted disc.
        # Its bytes and SHA are checked with every other staged member; no BCA value is invented.
        members.append((verified_bca_source(bca), "disc/bca.bin"))
    manifest.write_text("".join(f"{digest(path)}  {relative}\n" for path, relative in members), encoding="utf-8", newline="\n")
    with tarfile.open(archive, "w", format=tarfile.USTAR_FORMAT) as bundle:
        for path, relative in members: bundle.add(path, arcname=relative, recursive=False)
        bundle.add(manifest, arcname=".thor-sha256", recursive=False)
    return len(members)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial")
    parser.add_argument("--revision", type=int, choices=(1, 2), default=2)
    parser.add_argument("--work", type=Path)
    parser.add_argument("--bca", type=Path, help="Optional actual 64-byte disc BCA sidecar; copied verbatim as disc/bca.bin")
    parser.add_argument("--verify-only", action="store_true", help="Check local disc identity; do not contact a device")
    parser.add_argument("--disc", required=True, type=Path, help="Extracted DATA folder with files/ and sys/")
    args = parser.parse_args()
    bca = verified_bca_source(args.bca)
    if not args.verify_only and (not args.serial or any(c.isspace() for c in args.serial)):
        parser.error("Device staging requires a valid --serial")
    disc = args.disc.resolve()
    header = (disc / "sys/boot.bin").read_bytes()[:8]
    if len(header) != 8 or header[:6] != b"SMNE01" or header[7] != args.revision:
        raise SystemExit(f"This staging route requires the owner's USA revision {args.revision}.")
    for name, expected in EXPECTED_BY_REVISION[args.revision].items():
        if digest(disc / name) != expected: raise SystemExit(f"Wrong compiled game input: {name}")
    if bca is None:
        print("Readiness note: no BCA source supplied; disc verification metadata is missing. This is not a bootability claim.")
    else:
        print(f"Supplied 64-byte disc metadata SHA256={digest(bca)}; it will be copied verbatim.")
    if args.verify_only:
        print(f"Verified local USA revision {args.revision} executable and four REL hashes; no device contacted.")
        return
    command = [str(ROOT / ".tools/platform-tools/adb.exe"), "-s", args.serial]
    def output(*parts):
        return subprocess.check_output(command + list(parts), text=True).strip()
    if not output("shell", "pm", "path", PACKAGE).startswith("package:"):
        raise SystemExit("Install the private Thor NSMB APK before staging.")
    game_pid = subprocess.run(command + ["shell", "pidof", PACKAGE + ":game"], capture_output=True, text=True)
    if game_pid.stdout.strip(): raise SystemExit("Close the NSMB game before staging; no process was stopped.")
    parent = f"/sdcard/Android/data/{PACKAGE}/files"
    target = parent + "/game"
    exists = subprocess.run(command + ["shell", "test", "-e", target])
    if exists.returncode == 0: raise SystemExit("An NSMB game folder already exists; nothing was replaced.")
    tag = uuid.uuid4().hex
    incoming = parent + "/.game-incoming-" + tag
    remote_archive = parent + "/.thor-disc-" + tag + ".tar"
    work = (args.work or ROOT / ("build/nsmbw-thor/e1" if args.revision == 1 else "build/nsmbw-thor")).resolve()
    work.mkdir(parents=True, exist_ok=True)
    manifest = work / "disc-sha256.txt"
    archive = work / "discdata.tar"
    member_count = write_archive(disc, bca, manifest, archive)
    subprocess.run(command + ["shell", "mkdir", "-p", incoming], check=True)
    subprocess.run(command + ["push", str(archive), remote_archive], check=True)
    subprocess.run(command + ["shell", "tar", "-xf", remote_archive, "-C", incoming], check=True)
    # Paths are fixed app-owned names and a generated hex UUID; no supplied shell code is accepted.
    verified = subprocess.run(command + ["shell", f"cd {incoming} && sha256sum -c .thor-sha256"], capture_output=True, text=True)
    (work / "device-disc-verification.txt").write_text(verified.stdout + verified.stderr)
    if verified.returncode != 0: raise SystemExit("Device hashes differ; incomplete staging remains isolated.")
    subprocess.run(command + ["shell", f"if [ ! -e {target} ]; then mv {incoming} {target}; else exit 1; fi"], check=True)
    subprocess.run(command + ["shell", "rm", remote_archive], check=True)
    print(f"Staged and hash-verified {member_count} owner files in the isolated NSMB profile.")


if __name__ == "__main__":
    main()
