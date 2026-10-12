#!/usr/bin/env python3
"""Fetch benchmark sources, verify source SHA-256, decompress, verify CNF SHA-256."""
from __future__ import annotations

import argparse
import hashlib
import json
import lzma
import shutil
import urllib.request
from pathlib import Path

UA = "SatSolv-R7-benchmark-fetcher/1.0"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def download(url: str, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=300) as src, target.open("wb") as dst:
        shutil.copyfileobj(src, dst, length=1 << 20)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", type=Path, required=True)
    ap.add_argument("--dest", type=Path, required=True)
    args = ap.parse_args()
    manifest = json.loads(args.manifest.read_text())
    args.dest.mkdir(parents=True, exist_ok=True)
    sources = args.dest / ".sources"

    for item in manifest["instances"]:
        url = item.get("url")
        source_hash = item.get("source_sha256")
        content_hash = item.get("sha256")
        if not url or not content_hash:
            raise SystemExit(f"manifest entry lacks url/sha256: {item.get('name')}")

        source_name = item.get("source_filename") or Path(item["path"]).name
        source_path = sources / source_name
        target = args.dest / item["path"]

        if not source_path.exists():
            download(url, source_path)
        if source_hash:
            got_source = sha256(source_path)
            if got_source != source_hash:
                raise SystemExit(f"source SHA-256 mismatch {item['name']}: {got_source} != {source_hash}")

        target.parent.mkdir(parents=True, exist_ok=True)
        if source_name.endswith(".xz"):
            with lzma.open(source_path, "rb") as src, target.open("wb") as dst:
                shutil.copyfileobj(src, dst, length=1 << 20)
        else:
            shutil.copy2(source_path, target)

        got = sha256(target)
        if got != content_hash:
            raise SystemExit(f"CNF SHA-256 mismatch {item['name']}: {got} != {content_hash}")
        print(f"OK {item['name']} source={source_hash or 'unrecorded'} cnf={got}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
