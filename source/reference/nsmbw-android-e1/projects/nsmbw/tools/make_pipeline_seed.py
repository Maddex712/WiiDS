"""Merge aurora pipeline caches (Cache/pipeline_cache.db from devices that played NSMBW) into the
initial_pipeline_cache.db shipped with the game, so a first launch builds the game's shaders up front
instead of stuttering when an effect first appears.

    python make_pipeline_seed.py <out.db> <pipeline_cache.db> [<pipeline_cache.db> ...]

Each input's -wal file must sit next to it (sqlite applies it on open). The rows are portable GX
pipeline descriptions (no compiled code and no game data); aurora merges them into each player's own
cache at startup. Standard library only.
"""
import sqlite3
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out = Path(sys.argv[1])
    if out.exists():
        out.unlink()
    first = sqlite3.connect(sys.argv[2])
    first.execute(f"VACUUM INTO '{out.as_posix()}'")
    first.close()
    db = sqlite3.connect(out)
    for extra in sys.argv[3:]:
        db.execute("ATTACH DATABASE ? AS other", (extra,))
        before = db.execute("SELECT COUNT(*) FROM pipeline_cache").fetchone()[0]
        db.execute("INSERT OR IGNORE INTO pipeline_cache SELECT * FROM other.pipeline_cache")
        db.commit()
        after = db.execute("SELECT COUNT(*) FROM pipeline_cache").fetchone()[0]
        db.execute("DETACH DATABASE other")
        print(f"{extra}: +{after - before} pipelines")
    rows = db.execute("SELECT type, COUNT(*) FROM pipeline_cache GROUP BY type").fetchall()
    db.execute("PRAGMA journal_mode=DELETE")
    db.execute("VACUUM")
    db.close()
    print(f"{out}: {sum(n for _, n in rows)} pipelines by type {rows}, {out.stat().st_size // 1024} KiB")


if __name__ == "__main__":
    main()
