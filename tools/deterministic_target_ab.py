#!/usr/bin/env python3
"""Create and audit a same-binary deterministic target-phase qualification.

This tool deliberately separates causal-integrity checks from performance gates.
A negative benchmark is valid evidence; malformed or confounded evidence is not.
"""

import argparse
import hashlib
import json
from pathlib import Path

PINNED_KISSAT = "8af8e56f174b778aef3aa45af9f739b2a5f492c2"
DEFAULT_WARMUP_CONFLICTS = 1_000_000
TARGET_FLAGS = "--experimental-target-phases"
WARMUP_FLAG = "--search-warmup"
WALL_CLOCK_FLAG = "--search-warmup-seconds"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path):
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read valid JSON from {path}: {exc}") from exc


def validate_manifest(path: Path):
    document = load_json(path)
    instances = document.get("instances")
    if not isinstance(instances, list) or not instances:
        raise SystemExit("manifest must contain a non-empty instances array")
    names = set()
    for index, item in enumerate(instances):
        if not isinstance(item, dict):
            raise SystemExit(f"manifest instance {index} is not an object")
        for key in ("name", "path", "sha256", "expected"):
            if not isinstance(item.get(key), str) or not item[key]:
                raise SystemExit(f"manifest instance {index} has invalid {key}")
        if item["name"] in names:
            raise SystemExit(f"duplicate manifest instance: {item['name']}")
        names.add(item["name"])
        if len(item["sha256"]) != 64 or any(c not in "0123456789abcdefABCDEF" for c in item["sha256"]):
            raise SystemExit(f"invalid SHA-256 for {item['name']}")
        if item["expected"] not in ("SAT", "UNSAT"):
            raise SystemExit(f"invalid expected result for {item['name']}")
    return document


def require_file(path: Path, label: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_file():
        raise SystemExit(f"{label} is not a regular file: {resolved}")
    return resolved


def config_command(args) -> None:
    if args.warmup_conflicts <= 0:
        raise SystemExit("warmup conflicts must be positive")
    if not args.source_commit.strip():
        raise SystemExit("source commit must be non-empty")

    manifest = validate_manifest(args.manifest)
    binary = require_file(args.binary, "SatSolv binary")
    kissat = require_file(args.kissat, "Kissat binary")
    candidate_flags = [TARGET_FLAGS, WARMUP_FLAG, str(args.warmup_conflicts)]

    document = {
        "manifest_sha256": sha256(args.manifest),
        "instance_count": len(manifest["instances"]),
        "purpose": "same-binary conflict-only target-phase causal qualification",
        "contract": {
            "same_binary_control_candidate": True,
            "wall_clock_activation_allowed": False,
            "warmup_conflicts": args.warmup_conflicts,
            "candidate_flags": candidate_flags,
        },
        "arms": [
            {
                "name": "control",
                "kind": "satsolv",
                "binary": str(binary),
                "source_commit": args.source_commit,
                "flags": [],
            },
            {
                "name": "candidate",
                "kind": "satsolv",
                "binary": str(binary),
                "source_commit": args.source_commit,
                "flags": candidate_flags,
            },
            {
                "name": "kissat",
                "kind": "kissat",
                "binary": str(kissat),
                "source_commit": args.kissat_commit,
                "flags": [],
            },
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")
    print(json.dumps(document, indent=2))


def arm_map(config):
    arms = config.get("arms")
    if not isinstance(arms, list):
        return {}
    return {arm.get("name"): arm for arm in arms if isinstance(arm, dict) and isinstance(arm.get("name"), str)}


def stat_int(stats, name):
    value = stats.get(name, 0)
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def audit_command(args) -> None:
    result = load_json(args.result)
    errors = []
    pins = result.get("pins")
    config = pins.get("config") if isinstance(pins, dict) else None
    if not isinstance(config, dict):
        errors.append("result does not contain pins.config")
        config = {}
    arms = arm_map(config)
    control, candidate = arms.get("control"), arms.get("candidate")
    if not isinstance(control, dict) or not isinstance(candidate, dict):
        errors.append("config lacks control/candidate arms")
    else:
        if control.get("kind") != "satsolv" or candidate.get("kind") != "satsolv":
            errors.append("control and candidate must both be native SatSolv arms")
        if control.get("source_commit") != candidate.get("source_commit"):
            errors.append("control/candidate source commits differ")
        if control.get("binary") != candidate.get("binary"):
            errors.append("control/candidate binary paths differ")
        if control.get("binary_sha256") != candidate.get("binary_sha256"):
            errors.append("control/candidate executable digests differ")
        if control.get("flags") != []:
            errors.append("control arm must have no experimental flags")
        flags = candidate.get("flags")
        expected_warmup = config.get("contract", {}).get("warmup_conflicts") if isinstance(config.get("contract"), dict) else None
        expected_flags = [TARGET_FLAGS, WARMUP_FLAG, str(expected_warmup)]
        if flags != expected_flags:
            errors.append(f"candidate flags differ from conflict-only contract: {flags!r}")
        if isinstance(flags, list) and WALL_CLOCK_FLAG in flags:
            errors.append("candidate contains wall-clock activation")

    contract = config.get("contract")
    warmup = None
    if not isinstance(contract, dict):
        errors.append("missing deterministic-target contract")
    else:
        if contract.get("same_binary_control_candidate") is not True:
            errors.append("same-binary contract is not asserted")
        if contract.get("wall_clock_activation_allowed") is not False:
            errors.append("wall-clock activation is not forbidden")
        warmup = contract.get("warmup_conflicts")
        if not isinstance(warmup, int) or isinstance(warmup, bool) or warmup <= 0:
            errors.append("invalid conflict warmup in contract")
            warmup = None

    certification = result.get("certification")
    if not isinstance(certification, dict) or certification.get("passed") is not True:
        errors.append("paired campaign certification did not pass")

    rows = result.get("rows")
    if not isinstance(rows, list) or not rows:
        errors.append("result has no observations")
        rows = []

    checked_native = 0
    for row in rows:
        solvers = row.get("solvers") if isinstance(row, dict) else None
        if not isinstance(solvers, dict):
            errors.append("observation lacks solver map")
            continue
        for name in ("control", "candidate"):
            record = solvers.get(name)
            if not isinstance(record, dict) or record.get("timed_out"):
                continue
            stats = record.get("stats")
            if not isinstance(stats, dict):
                errors.append(f"{row.get('instance', '?')} {name}: missing native stats")
                continue
            checked_native += 1
            activations = stat_int(stats, "search_activations")
            captures = stat_int(stats, "target_captures")
            decisions = stat_int(stats, "target_decisions")
            resets = stat_int(stats, "target_resets")
            conflicts = stat_int(stats, "conflicts")
            if None in (activations, captures, decisions, resets, conflicts):
                errors.append(f"{row.get('instance', '?')} {name}: malformed activation statistics")
                continue
            if name == "control":
                if any((activations, captures, decisions, resets)):
                    errors.append(f"{row.get('instance', '?')} control unexpectedly exercised target policy")
            else:
                if activations not in (0, 1):
                    errors.append(f"{row.get('instance', '?')} candidate activated search {activations} times")
                if warmup is not None and conflicts < warmup and activations != 0:
                    errors.append(f"{row.get('instance', '?')} candidate activated before conflict warmup")
                if activations == 0 and any((captures, decisions, resets)):
                    errors.append(f"{row.get('instance', '?')} candidate used target policy before activation")

    audit = {
        "causal_integrity_passed": not errors,
        "errors": errors,
        "checked_native_observations": checked_native,
        "performance": {
            "summary": result.get("summary"),
            "lost_control_solves": result.get("lost_control_solves"),
            "gained_control_solves": result.get("gained_control_solves"),
            "paired_numerical_gate": result.get("paired_numerical_gate"),
            "strict_kissat_numerical_superiority": result.get("strict_kissat_numerical_superiority"),
            "statistical_superiority_established": result.get("statistical_superiority_established"),
            "general_sota_established": result.get("general_sota_established"),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(audit, indent=2) + "\n")
    print(json.dumps(audit, indent=2))
    if errors:
        raise SystemExit("causal-integrity audit failed")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    make = sub.add_parser("config", help="create a paired_campaign.py config")
    make.add_argument("--manifest", type=Path, required=True)
    make.add_argument("--binary", type=Path, required=True)
    make.add_argument("--kissat", type=Path, required=True)
    make.add_argument("--source-commit", required=True)
    make.add_argument("--kissat-commit", default=PINNED_KISSAT)
    make.add_argument("--warmup-conflicts", type=int, default=DEFAULT_WARMUP_CONFLICTS)
    make.add_argument("--output", type=Path, required=True)
    make.set_defaults(func=config_command)

    audit = sub.add_parser("audit", help="audit causal integrity of paired results")
    audit.add_argument("--result", type=Path, required=True)
    audit.add_argument("--output", type=Path, required=True)
    audit.set_defaults(func=audit_command)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
