"""Pinned ASR asset metadata and offline verification (Python standard library)."""
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def manifest():
    return json.loads((HERE / "manifest.json").read_text(encoding="utf-8"))


def verify(path, pin):
    path = Path(path)
    if not path.is_file():
        raise ValueError(f"Missing file: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    if path.stat().st_size != pin["size_bytes"] or digest.hexdigest() != pin["sha256"]:
        raise ValueError(f"Size/SHA256 mismatch: {path}")


def outside_repo(path):
    path = Path(path).resolve()
    if path == HERE.parents[1] or HERE.parents[1] in path.parents:
        raise ValueError("Keep downloaded assets outside the repository")
    return path
