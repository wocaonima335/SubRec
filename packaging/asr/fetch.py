#!/usr/bin/env python3
"""Verify cached approved assets; network is enabled only by --download."""
import argparse
from pathlib import Path
import shutil
import tempfile
import urllib.request

from assets import manifest, outside_repo, verify


def fetch(cache, asset_ids, download=False):
    cache = outside_repo(cache)
    pins = manifest()["assets"]
    for asset_id in asset_ids:
        pin = pins[asset_id]
        target = cache / pin["cache_path"]
        if target.exists():
            verify(target, pin)  # Never replace a mismatching cached file silently.
        elif not download:
            raise ValueError(f"Missing cached asset: {target}; verification is offline")
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            # In the same directory for atomic publication; no unverified final file.
            with tempfile.TemporaryDirectory(prefix=".asr-fetch-", dir=target.parent) as temp:
                partial = Path(temp) / "download"
                with urllib.request.urlopen(pin["url"], timeout=60) as source, partial.open("wb") as dest:
                    shutil.copyfileobj(source, dest)
                verify(partial, pin)
                partial.replace(target)
        print(f"Verified {asset_id}: {target}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True, help="directory outside the repo")
    parser.add_argument("--asset", action="append", required=True,
                        choices=tuple(manifest()["assets"]), help="repeat for selected pinned assets")
    parser.add_argument("--download", action="store_true", help="explicitly allow fetching missing files")
    args = parser.parse_args()
    try:
        fetch(args.cache, args.asset, args.download)
    except (OSError, ValueError) as error:
        parser.exit(1, f"ASR fetch failed: {error}\n")


if __name__ == "__main__":
    main()
