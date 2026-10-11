#!/usr/bin/env python3
"""Resolve the frozen SAT Competition candidate list into an exact SHA-256 manifest.

Downloads only the preselected candidates from the official SAT Competition 2023
Main Track URI list exposed by the Global Benchmark Database. Both compressed
source bytes and decompressed DIMACS bytes are SHA-256 hashed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import lzma
import shutil
import urllib.parse
import urllib.request
from pathlib import Path

URI_LIST = "https://benchmark-database.de/getinstances?track=main_2023"
UA = "SatSolv-R7-benchmark-resolver/1.0"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch_bytes(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def basename(url: str) -> str:
    return urllib.parse.unquote(Path(urllib.parse.urlparse(url).path).name)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--candidates", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--work", type=Path, default=Path(".benchmark-resolution"))
    args = ap.parse_args()

    candidates_doc = json.loads(args.candidates.read_text())
    candidates = candidates_doc["instances"]
    if len(candidates) != 30:
        raise SystemExit(f"expected frozen 30 candidates, found {len(candidates)}")
    if sum(x["expected"] == "SAT" for x in candidates) != 15 or sum(x["expected"] == "UNSAT" for x in candidates) != 15:
        raise SystemExit("candidate SAT/UNSAT balance changed")

    body = fetch_bytes(URI_LIST).decode("utf-8")
    urls = [line.strip() for line in body.splitlines() if line.strip()]
    by_name = {}
    for url in urls:
        by_name.setdefault(basename(url), []).append(url)

    args.work.mkdir(parents=True, exist_ok=True)
    resolved = []
    for index, c in enumerate(candidates, 1):
        filename = c["filename"]
        matches = by_name.get(filename, [])
        if len(matches) != 1:
            raise SystemExit(f"{filename}: expected exactly one official URL, found {len(matches)}")
        url = matches[0]
        compressed = args.work / filename
        print(f"[{index:02d}/{len(candidates)}] {filename}", flush=True)
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=300) as src, compressed.open("wb") as dst:
            shutil.copyfileobj(src, dst, length=1 << 20)
        source_hash = sha256(compressed)

        local_name = filename[:-3] if filename.endswith(".xz") else filename
        decompressed = args.work / local_name
        if filename.endswith(".xz"):
            with lzma.open(compressed, "rb") as src, decompressed.open("wb") as dst:
                shutil.copyfileobj(src, dst, length=1 << 20)
        else:
            shutil.copy2(compressed, decompressed)
        content_hash = sha256(decompressed)

        resolved.append({
            "name": local_name,
            "path": f"competition/{local_name}",
            "expected": c["expected"],
            "family": c["family"],
            "minisat1m": c["minisat1m"],
            "gbd_hash": c["gbd_hash"],
            "url": url,
            "source_filename": filename,
            "source_sha256": source_hash,
            "sha256": content_hash,
            "source_bytes": compressed.stat().st_size,
            "cnf_bytes": decompressed.stat().st_size,
            "competition": "SAT Competition 2023",
            "track": "main_2023"
        })
        compressed.unlink(missing_ok=True)
        decompressed.unlink(missing_ok=True)

    output = {
        "schema": 2,
        "status": "resolved",
        "source_uri_list": URI_LIST,
        "selection": candidates_doc.get("selection_rule"),
        "count": len(resolved),
        "sat_count": sum(x["expected"] == "SAT" for x in resolved),
        "unsat_count": sum(x["expected"] == "UNSAT" for x in resolved),
        "instances": resolved,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
    print("---BEGIN-COMPETITION-MANIFEST---")
    print(args.output.read_text(), end="")
    print("---END-COMPETITION-MANIFEST---")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
