#!/usr/bin/env python3
"""Create and audit the same-binary SatSolv dual-search qualification.

The control is the qualified delayed target-phase policy.  The candidate is the
same executable with only focused-VMTF/stable-EVSIDS mode switching enabled.
A negative performance result remains valid evidence; causal-integrity or
correctness failures do not.
"""

import argparse
import hashlib
import json
from pathlib import Path

PINNED_KISSAT = "8af8e56f174b778aef3aa45af9f739b2a5f492c2"
DEFAULT_WARMUP = 1_000_000
DEFAULT_MODE_INTERVAL = 1_000
TARGET = "--experimental-target-phases"
WARMUP = "--search-warmup"
DUAL = "--experimental-dual-search"
INTERVAL = "--dual-search-interval"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load(path: Path):
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read valid JSON from {path}: {exc}") from exc


def regular(path: Path, label: str) -> Path:
    path = path.resolve()
    if not path.is_file():
        raise SystemExit(f"{label} is not a regular file: {path}")
    return path


def validate_manifest(path: Path):
    doc = load(path)
    rows = doc.get("instances")
    if not isinstance(rows, list) or not rows:
        raise SystemExit("manifest must contain instances")
    seen = set()
    for i, row in enumerate(rows):
        if not isinstance(row, dict):
            raise SystemExit(f"manifest row {i} is not an object")
        for key in ("name", "path", "sha256", "expected"):
            if not isinstance(row.get(key), str) or not row[key]:
                raise SystemExit(f"manifest row {i} has invalid {key}")
        if row["name"] in seen:
            raise SystemExit(f"duplicate manifest instance {row['name']}")
        seen.add(row["name"])
        if row["expected"] not in ("SAT", "UNSAT"):
            raise SystemExit(f"invalid expected result for {row['name']}")
    return doc


def make_config(args) -> None:
    if args.warmup <= 0 or args.mode_interval <= 0:
        raise SystemExit("warmup and mode interval must be positive")
    if not args.source_commit.strip():
        raise SystemExit("source commit must be non-empty")
    manifest = validate_manifest(args.manifest)
    satsolv = regular(args.binary, "SatSolv binary")
    kissat = regular(args.kissat, "Kissat binary")
    control_flags = [TARGET, WARMUP, str(args.warmup)]
    candidate_flags = [*control_flags, DUAL, INTERVAL, str(args.mode_interval)]
    document = {
        "manifest_sha256": sha256(args.manifest),
        "instance_count": len(manifest["instances"]),
        "purpose": "same-binary focused-VMTF/stable-EVSIDS causal qualification",
        "contract": {
            "same_binary_control_candidate": True,
            "control_is_delayed_target_winner": True,
            "warmup_conflicts": args.warmup,
            "mode_interval_conflicts": args.mode_interval,
            "control_flags": control_flags,
            "candidate_flags": candidate_flags,
            "changed_search_dimension": "decision-variable ordering and deterministic search-mode switching only",
        },
        "arms": [
            {"name": "control", "kind": "satsolv", "binary": str(satsolv),
             "source_commit": args.source_commit, "flags": control_flags},
            {"name": "candidate", "kind": "satsolv", "binary": str(satsolv),
             "source_commit": args.source_commit, "flags": candidate_flags},
            {"name": "kissat", "kind": "kissat", "binary": str(kissat),
             "source_commit": args.kissat_commit, "flags": []},
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")
    print(json.dumps(document, indent=2))


def arm_map(config):
    arms = config.get("arms")
    if not isinstance(arms, list):
        return {}
    return {a.get("name"): a for a in arms if isinstance(a, dict) and isinstance(a.get("name"), str)}


def integer(stats, key):
    value = stats.get(key)
    return value if isinstance(value, int) and not isinstance(value, bool) and value >= 0 else None


def audit(args) -> None:
    result = load(args.result)
    errors = []
    config = result.get("pins", {}).get("config") if isinstance(result.get("pins"), dict) else None
    if not isinstance(config, dict):
        errors.append("result lacks pins.config")
        config = {}
    arms = arm_map(config)
    control, candidate = arms.get("control"), arms.get("candidate")
    contract = config.get("contract") if isinstance(config.get("contract"), dict) else {}
    warmup = contract.get("warmup_conflicts")
    interval = contract.get("mode_interval_conflicts")
    expected_control = [TARGET, WARMUP, str(warmup)]
    expected_candidate = [*expected_control, DUAL, INTERVAL, str(interval)]

    if not isinstance(control, dict) or not isinstance(candidate, dict):
        errors.append("missing native control/candidate arms")
    else:
        if control.get("kind") != "satsolv" or candidate.get("kind") != "satsolv":
            errors.append("control/candidate must both be SatSolv")
        if control.get("source_commit") != candidate.get("source_commit"):
            errors.append("control/candidate source commits differ")
        if control.get("binary") != candidate.get("binary"):
            errors.append("control/candidate binary paths differ")
        if control.get("binary_sha256") != candidate.get("binary_sha256"):
            errors.append("control/candidate executable digests differ")
        if control.get("flags") != expected_control:
            errors.append(f"control flags violate contract: {control.get('flags')!r}")
        if candidate.get("flags") != expected_candidate:
            errors.append(f"candidate flags violate contract: {candidate.get('flags')!r}")
    if contract.get("same_binary_control_candidate") is not True:
        errors.append("same-binary contract not asserted")
    if not isinstance(warmup, int) or isinstance(warmup, bool) or warmup <= 0:
        errors.append("invalid target warmup")
    if not isinstance(interval, int) or isinstance(interval, bool) or interval <= 0:
        errors.append("invalid dual-search interval")

    certification = result.get("certification")
    if not isinstance(certification, dict) or certification.get("passed") is not True:
        errors.append("paired campaign certification did not pass")
    rows = result.get("rows")
    if not isinstance(rows, list) or not rows:
        errors.append("result contains no observations")
        rows = []

    checked = 0
    for row in rows:
        solvers = row.get("solvers") if isinstance(row, dict) else None
        if not isinstance(solvers, dict):
            errors.append("row lacks solver map")
            continue
        for name in ("control", "candidate"):
            record = solvers.get(name)
            if not isinstance(record, dict) or record.get("timed_out"):
                continue
            stats = record.get("stats")
            if not isinstance(stats, dict):
                errors.append(f"{row.get('instance', '?')} {name}: missing native stats")
                continue
            checked += 1
            keys = ("conflicts", "decisions", "search_mode_switches", "focused_conflicts",
                    "stable_conflicts", "focused_decisions", "stable_decisions",
                    "vmtf_bumps", "vmtf_search_steps", "search_activations",
                    "target_captures", "target_decisions", "target_resets")
            values = {key: integer(stats, key) for key in keys}
            if any(value is None for value in values.values()):
                errors.append(f"{row.get('instance', '?')} {name}: malformed dual-search/target telemetry")
                continue
            if name == "control":
                dual_keys = ("search_mode_switches", "focused_conflicts", "stable_conflicts",
                             "focused_decisions", "stable_decisions", "vmtf_bumps", "vmtf_search_steps")
                if any(values[key] for key in dual_keys):
                    errors.append(f"{row.get('instance', '?')} control unexpectedly exercised dual search")
            else:
                if values["focused_conflicts"] + values["stable_conflicts"] != values["conflicts"]:
                    errors.append(f"{row.get('instance', '?')} candidate conflict accounting mismatch")
                if values["focused_decisions"] + values["stable_decisions"] != values["decisions"]:
                    errors.append(f"{row.get('instance', '?')} candidate decision accounting mismatch")
                if values["focused_decisions"] and values["vmtf_search_steps"] == 0:
                    errors.append(f"{row.get('instance', '?')} candidate focused decisions without VMTF traversal")
                if values["search_mode_switches"] > values["conflicts"] // interval:
                    errors.append(f"{row.get('instance', '?')} candidate switched modes too frequently")
            # Both arms use the same delayed target-phase policy.
            if values["conflicts"] < warmup and values["search_activations"] != 0:
                errors.append(f"{row.get('instance', '?')} {name}: target search activated before warmup")
            if values["search_activations"] == 0 and any(values[k] for k in ("target_captures", "target_decisions", "target_resets")):
                errors.append(f"{row.get('instance', '?')} {name}: target policy used before activation")

    summary = result.get("summary") if isinstance(result.get("summary"), dict) else {}
    control_summary = summary.get("control") if isinstance(summary.get("control"), dict) else {}
    candidate_summary = summary.get("candidate") if isinstance(summary.get("candidate"), dict) else {}
    lost = result.get("lost_control_solves")
    gained = result.get("gained_control_solves")
    adoption_gate = (
        not errors and isinstance(lost, list) and not lost and isinstance(gained, list) and bool(gained)
        and isinstance(candidate_summary.get("solved"), int) and candidate_summary["solved"] >= 11
        and isinstance(control_summary.get("solved"), int) and candidate_summary["solved"] > control_summary["solved"]
        and candidate_summary.get("par2", float("inf")) <= control_summary.get("par2", float("inf"))
    )
    document = {
        "causal_integrity_passed": not errors,
        "errors": errors,
        "checked_native_observations": checked,
        "adoption_gate": adoption_gate,
        "adoption_rule": "no lost control solves; at least one gained solve; candidate >=11 solves; candidate solves > control; candidate PAR-2 <= control",
        "performance": {
            "summary": summary,
            "lost_control_solves": lost,
            "gained_control_solves": gained,
            "paired_numerical_gate": result.get("paired_numerical_gate"),
            "strict_kissat_numerical_superiority": result.get("strict_kissat_numerical_superiority"),
            "statistical_superiority_established": result.get("statistical_superiority_established"),
            "general_sota_established": result.get("general_sota_established"),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")
    print(json.dumps(document, indent=2))
    if errors:
        raise SystemExit("dual-search causal-integrity audit failed")
    if args.require_adoption_gate and not adoption_gate:
        raise SystemExit("dual-search performance adoption gate failed; preserve qualified control")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    make = sub.add_parser("config")
    make.add_argument("--manifest", type=Path, required=True)
    make.add_argument("--binary", type=Path, required=True)
    make.add_argument("--kissat", type=Path, required=True)
    make.add_argument("--source-commit", required=True)
    make.add_argument("--kissat-commit", default=PINNED_KISSAT)
    make.add_argument("--warmup", type=int, default=DEFAULT_WARMUP)
    make.add_argument("--mode-interval", type=int, default=DEFAULT_MODE_INTERVAL)
    make.add_argument("--output", type=Path, required=True)
    make.set_defaults(func=make_config)
    check = sub.add_parser("audit")
    check.add_argument("--result", type=Path, required=True)
    check.add_argument("--output", type=Path, required=True)
    check.add_argument("--require-adoption-gate", action="store_true")
    check.set_defaults(func=audit)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
