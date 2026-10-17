#!/usr/bin/env python3
"""Construct and independently audit same-binary search-mode sentinel comparisons.

This exploratory five-arm screen cannot qualify an optimization for adoption.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path

import benchmark

SENTINELS = (
    "vmpc_28.shuffled-as.sat05-1957.cnf",
    "stb_792_333.apx_1_DS-ST.cnf",
    "ibm-2004-03-k70.cnf",
    "clqcolor-08-06-07.shuffled-as.sat05-1257.cnf",
)
CONTROL = ["--experimental-target-phases", "--search-warmup", "1000000"]
VARIANTS = (
    ("control", None),
    ("candidate", 1000),
    ("focus_only", 1000000000000),
    ("switch_10k", 10000),
    ("switch_100k", 100000),
)


def expected_flags(interval):
    return list(CONTROL) if interval is None else [
        *CONTROL, "--experimental-dual-search", "--dual-search-interval", str(interval)
    ]


def prepare(args):
    full = json.loads(args.manifest.read_text())
    rows = {row["name"]: row for row in full["instances"]}
    if len(rows) != len(full["instances"]) or any(name not in rows for name in SENTINELS):
        raise SystemExit("missing/duplicate sentinel in frozen manifest")
    binary = args.binary.resolve()
    if not binary.is_file():
        raise SystemExit("native executable not found")
    if not args.source_commit:
        raise SystemExit("source commit required")
    subset = {"count": len(SENTINELS), "instances": [rows[name] for name in SENTINELS]}
    args.output.mkdir(parents=True, exist_ok=True)
    selected = args.output / "sentinel_manifest.json"
    selected.write_text(json.dumps(subset, indent=2) + "\n")
    config = {
        "purpose": "exploratory same-binary variable-choice and mode-cadence ablation",
        "source_manifest_sha256": benchmark.sha256(args.manifest),
        "manifest_sha256": benchmark.sha256(selected),
        "instance_count": len(SENTINELS),
        "source_commit": args.source_commit,
        "arms": [
            {"name": name, "kind": "satsolv", "binary": str(binary),
             "source_commit": args.source_commit, "flags": expected_flags(interval)}
            for name, interval in VARIANTS
        ],
    }
    (args.output / "config.json").write_text(json.dumps(config, indent=2) + "\n")
    print("Pinned source manifest", config["source_manifest_sha256"])
    print("Derived selected manifest", config["manifest_sha256"])


def audit(args):
    result = json.loads(args.result.read_text())
    problems = []
    config = result.get("pins", {}).get("config", {})
    arms = config.get("arms", [])
    if [a.get("name") for a in arms] != [x[0] for x in VARIANTS]:
        problems.append("arm names/order changed")
    if not result.get("certification", {}).get("passed"):
        problems.append("independent SAT/DRAT certification failed")
    digests = {a.get("binary_sha256") for a in arms}
    commits = {a.get("source_commit") for a in arms}
    binaries = {a.get("binary") for a in arms}
    if len(digests) != 1 or None in digests or len(commits) != 1 or len(binaries) != 1:
        problems.append("different native binary, digest or source commit across arms")
    for i, (name, interval) in enumerate(VARIANTS):
        if i >= len(arms):
            continue
        a = arms[i]
        if a.get("kind") != "satsolv" or a.get("flags") != expected_flags(interval):
            problems.append(f"changed config: {name}")
    rows = result.get("rows", [])
    if [r.get("instance") for r in rows] != list(SENTINELS):
        problems.append("unexpected or missing sentinel")
    observations = []
    for row in rows:
        item = {"instance": row.get("instance"), "variants": {}}
        for name, interval in VARIANTS:
            rec = row.get("solvers", {}).get(name)
            if not isinstance(rec, dict):
                problems.append(f"missing arm {row.get('instance')} {name}")
                continue
            stats = rec.get("stats", {})
            item["variants"][name] = {
                "result": rec.get("result"), "elapsed": rec.get("elapsed"),
                "verified": rec.get("verified"), "timed_out": rec.get("timed_out"),
                "conflicts": stats.get("conflicts"), "decisions": stats.get("decisions"),
                "restarts": stats.get("restarts"),
                "mode_switches": stats.get("search_mode_switches"),
                "focused_decisions": stats.get("focused_decisions"),
                "stable_decisions": stats.get("stable_decisions"),
                "vmtf_search_steps": stats.get("vmtf_search_steps"),
            }
            if rec.get("timed_out"):
                # External kills do not produce trustworthy end-of-run telemetry.
                continue
            if not isinstance(stats, dict) or not stats:
                problems.append(f"missing native stats for {name}")
                continue
            conflicts = stats.get("conflicts")
            decisions = stats.get("decisions")
            modes = [stats.get("focused_decisions"), stats.get("stable_decisions")]
            if any(not isinstance(x, int) or x < 0 for x in [conflicts, decisions, *modes]):
                problems.append(f"malformed decision/conflict counters for {name}")
            elif interval is None and any(stats.get(k, 0) for k in (
                "search_mode_switches", "focused_decisions", "stable_decisions", "vmtf_search_steps"
            )):
                problems.append("baseline entered experimental dual mode")
            elif interval is not None:
                if sum(modes) != decisions:
                    problems.append(f"mode decisions do not sum to total for {name}")
                if stats.get("search_mode_switches", 0) > conflicts // interval:
                    problems.append(f"mode switching rate inconsistent for {name}")
        observations.append(item)
    evidence = {
        "status": "PASS" if not problems else "FAIL",
        "scope": "exploratory sentinel-only; no performance adoption or statistical claims",
        "causal_integrity_passed": not problems,
        "errors": problems,
        "summary": result.get("summary"),
        "observations": observations,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps(evidence, indent=2))
    if problems:
        raise SystemExit("search sentinel causal-integrity failure")


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="action", required=True)
    a = sub.add_parser("prepare")
    a.add_argument("--manifest", type=Path, required=True)
    a.add_argument("--binary", type=Path, required=True)
    a.add_argument("--source-commit", required=True)
    a.add_argument("--output", type=Path, required=True)
    a.set_defaults(func=prepare)
    a = sub.add_parser("audit")
    a.add_argument("--result", type=Path, required=True)
    a.add_argument("--output", type=Path, required=True)
    a.set_defaults(func=audit)
    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
