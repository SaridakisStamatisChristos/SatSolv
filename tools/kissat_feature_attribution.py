#!/usr/bin/env python3
"""Same-binary structural feature attribution for pinned Kissat 4.0.4.

This is a diagnostic necessity experiment on six frozen cases where default
Kissat previously solved and SatSolv did not.  It does not benchmark SatSolv
and it does not authorize an implementation by itself.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import tempfile
from pathlib import Path

import benchmark


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text())


def load_spec(path: Path) -> dict:
    spec = load_json(path)
    if spec.get("schema_version") != 1:
        raise SystemExit("unsupported attribution spec schema")
    if not spec.get("instances") or not spec.get("arms"):
        raise SystemExit("empty attribution spec")
    names = [a.get("name") for a in spec["arms"]]
    if len(names) != len(set(names)) or "control" not in names:
        raise SystemExit("invalid or duplicate attribution arms")
    if spec["arms"][0]["name"] != "control" or spec["arms"][0].get("flags") != []:
        raise SystemExit("control arm must be first and have no flags")
    return spec


def manifest_item(spec: dict, manifest_path: Path, case_name: str) -> dict:
    if sha256(manifest_path) != spec["manifest_sha256"]:
        raise SystemExit("frozen competition manifest SHA-256 mismatch")
    manifest = load_json(manifest_path)
    item = next((x for x in manifest["instances"] if x["name"] == case_name), None)
    if item is None or case_name not in spec["instances"]:
        raise SystemExit(f"case not pre-registered: {case_name}")
    if item.get("expected") not in ("SAT", "UNSAT"):
        raise SystemExit(f"case lacks definitive frozen truth: {case_name}")
    return item


def verify_record(record: dict, expected: str) -> list[str]:
    errors: list[str] = []
    elapsed = record.get("elapsed")
    if record.get("crashed"):
        errors.append(f"{record['arm']}: crashed")
    if not isinstance(elapsed, (int, float)) or not math.isfinite(elapsed) or elapsed < 0:
        errors.append(f"{record['arm']}: invalid timing")
    result = record.get("result")
    if result in ("SAT", "UNSAT"):
        if result != expected:
            errors.append(f"{record['arm']}: truth disagreement {result} != {expected}")
        if not record.get("verified"):
            errors.append(f"{record['arm']}: definitive result not independently verified")
        if record.get("exit_code") != (10 if result == "SAT" else 20):
            errors.append(f"{record['arm']}: invalid definitive exit code")
    elif result == "TIMEOUT":
        if not record.get("timed_out"):
            errors.append(f"{record['arm']}: invalid timeout classification")
    else:
        errors.append(f"{record['arm']}: unsupported non-definitive result {result!r}")
    return errors


def run_arm(binary: Path, verifier: Path, cnf: Path, arm: dict, expected: str,
            seed: int, solver_seconds: float, proof_seconds: float,
            memory_gib: float, work: Path) -> dict:
    name = arm["name"]
    proof = work / f"{name}.drat"
    proof.unlink(missing_ok=True)
    checker = Path(__file__).resolve().parent / "check_model.py"
    command = [str(binary), f"--seed={seed}", *arm.get("flags", []), str(cnf), str(proof)]
    raw = benchmark.run_cmd(command, solver_seconds, int(memory_gib * 1024**3))
    result = "TIMEOUT" if raw["timed_out"] else benchmark.classify(raw["stdout"])
    verified = False
    verification = {"exit_code": None, "stdout": "", "stderr": "not run"}
    if result == "SAT":
        verified, verification = benchmark.verify_sat(checker, cnf, raw["stdout"], work)
    elif result == "UNSAT":
        verified, verification = benchmark.verify_unsat(verifier, cnf, proof, proof_seconds)
    record = dict(raw)
    record.update({
        "arm": name,
        "mechanism": arm.get("mechanism"),
        "flags": list(arm.get("flags", [])),
        "command": command,
        "result": result,
        "verified": verified,
        "verification": verification,
        "proof_bytes": proof.stat().st_size if proof.exists() else 0,
        "binary_sha256": benchmark.sha256(binary),
    })
    record["errors"] = verify_record(record, expected)
    return record


def definitive(record: dict) -> bool:
    return record["result"] in ("SAT", "UNSAT") and bool(record["verified"])


def effect(control: dict, ablated: dict, ratio_threshold: float) -> dict:
    c = definitive(control)
    a = definitive(ablated)
    ratio = None
    if c and a:
        if control["result"] != ablated["result"]:
            return {"classification": "INTEGRITY_CONTRADICTION", "ratio": None}
        ratio = float(ablated["elapsed"]) / max(float(control["elapsed"]), 1e-9)
        if ratio >= ratio_threshold:
            cls = "MATERIAL_DEGRADATION"
        elif ratio <= 1.0 / ratio_threshold:
            cls = "MATERIAL_REVERSE"
        else:
            cls = "NEUTRAL"
    elif c and not a:
        cls = "MATERIAL_DEGRADATION"
    elif a and not c:
        cls = "MATERIAL_REVERSE"
    else:
        cls = "UNINFORMATIVE"
    return {"classification": cls, "ratio": ratio}


def case_command(args: argparse.Namespace) -> int:
    spec = load_spec(args.spec)
    item = manifest_item(spec, args.manifest, args.case_name)
    budgets = spec["budgets"]
    solver_seconds = float(budgets["solver_seconds"])
    proof_seconds = float(budgets["proof_seconds"])
    memory_gib = float(budgets["memory_gib"])
    seed = int(budgets["seed"])
    binary = args.kissat.resolve()
    verifier = args.drat_trim.resolve()
    cnf = (args.root / item["path"]).resolve()
    if not binary.is_file() or not verifier.is_file() or not cnf.is_file():
        raise SystemExit("missing comparator, verifier, or benchmark input")
    if benchmark.sha256(cnf) != item["sha256"]:
        raise SystemExit("benchmark CNF SHA-256 mismatch")

    # Rotate the exact same pre-registered arm order by case index to reduce
    # systematic position effects without data-dependent scheduling.
    arms = list(spec["arms"])
    offset = spec["instances"].index(args.case_name) % len(arms)
    order = arms[offset:] + arms[:offset]
    records: dict[str, dict] = {}
    errors: list[str] = []
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        for arm in order:
            record = run_arm(binary, verifier, cnf, arm, item["expected"], seed,
                             solver_seconds, proof_seconds, memory_gib, work)
            records[arm["name"]] = record
            errors.extend(record["errors"])
            print(args.case_name, arm["name"], record["result"],
                  round(record["elapsed"], 3), "verified", record["verified"], flush=True)

    digests = {r["binary_sha256"] for r in records.values()}
    if len(digests) != 1:
        errors.append("arms did not use one exact Kissat executable")
    control = records["control"]
    if not definitive(control):
        errors.append("default Kissat control failed to reproduce its frozen decisive win")
    effects = {}
    for arm in spec["arms"]:
        if arm["name"] == "control":
            continue
        e = effect(control, records[arm["name"]], float(spec["decision_rule"]["material_ratio"]))
        effects[arm["name"]] = e
        if e["classification"] == "INTEGRITY_CONTRADICTION":
            errors.append(f"{arm['name']}: verified truth contradiction")

    document = {
        "case": item,
        "kissat_commit": spec["kissat_commit"],
        "manifest_sha256": spec["manifest_sha256"],
        "binary_sha256": next(iter(digests)) if len(digests) == 1 else None,
        "execution_order": [a["name"] for a in order],
        "records": records,
        "effects": effects,
        "integrity_passed": not errors,
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")
    print(json.dumps({"case": args.case_name, "effects": effects,
                      "integrity_passed": not errors, "errors": errors}, indent=2))
    return 0 if not errors else 2


def aggregate_command(args: argparse.Namespace) -> int:
    spec = load_spec(args.spec)
    docs = []
    for path in sorted(args.inputs.rglob("*.json")):
        try:
            d = load_json(path)
        except (json.JSONDecodeError, OSError):
            continue
        if isinstance(d, dict) and isinstance(d.get("case"), dict) and d["case"].get("name") in spec["instances"]:
            docs.append(d)
    by_name = {d["case"]["name"]: d for d in docs}
    errors = []
    missing = sorted(set(spec["instances"]) - set(by_name))
    if missing:
        errors.append("missing case evidence: " + ", ".join(missing))
    if len(docs) != len(by_name):
        errors.append("duplicate case evidence")
    for name, d in sorted(by_name.items()):
        if not d.get("integrity_passed"):
            errors.append(f"{name}: per-case integrity failed")

    mechanism_rows = {}
    for arm in spec["arms"]:
        name = arm["name"]
        if name == "control":
            continue
        degrade, reverse, neutral, uninformative = [], [], [], []
        for case in spec["instances"]:
            if case not in by_name:
                continue
            cls = by_name[case]["effects"][name]["classification"]
            target = {
                "MATERIAL_DEGRADATION": degrade,
                "MATERIAL_REVERSE": reverse,
                "NEUTRAL": neutral,
                "UNINFORMATIVE": uninformative,
            }.get(cls)
            if target is None:
                errors.append(f"{case}/{name}: invalid classification {cls}")
            else:
                target.append(case)
        rule = spec["decision_rule"]
        passes = (
            len(degrade) >= int(rule["minimum_degradations_per_mechanism"])
            and (not rule["require_degradations_exceed_reversals"] or len(degrade) > len(reverse))
        )
        mechanism_rows[name] = {
            "mechanism": arm.get("mechanism"),
            "flags": arm.get("flags", []),
            "material_degradations": degrade,
            "material_reversals": reverse,
            "neutral": neutral,
            "uninformative": uninformative,
            "attribution_threshold_passed": passes,
            "broad_interaction_arm": name == "no_simplify",
        }

    narrow_candidates = [
        name for name, row in mechanism_rows.items()
        if row["attribution_threshold_passed"] and not row["broad_interaction_arm"]
    ]
    broad_signal = mechanism_rows.get("no_simplify", {}).get("attribution_threshold_passed", False)
    summary = {
        "integrity_passed": not errors,
        "integrity_errors": errors,
        "case_count": len(by_name),
        "mechanisms": mechanism_rows,
        "narrow_attribution_candidates": narrow_candidates,
        "broad_simplification_interaction_signal": broad_signal,
        "decision": (
            "VALIDATE_NARROW_CANDIDATES_ON_HOLDOUT" if narrow_candidates
            else "DECOMPOSE_SIMPLIFICATION_INTERACTION" if broad_signal
            else "NO_ATTRIBUTED_STRUCTURAL_MECHANISM"
        ),
        "claim_boundary": "This is frozen-corpus necessity attribution. A passing narrow arm still requires separate holdout validation before SatSolv implementation/adoption.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if not errors else 2


def main() -> int:
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="command", required=True)
    case = sub.add_parser("case")
    case.add_argument("--spec", type=Path, required=True)
    case.add_argument("--manifest", type=Path, required=True)
    case.add_argument("--case-name", required=True)
    case.add_argument("--root", type=Path, required=True)
    case.add_argument("--kissat", type=Path, required=True)
    case.add_argument("--drat-trim", type=Path, required=True)
    case.add_argument("--output", type=Path, required=True)
    case.set_defaults(func=case_command)
    agg = sub.add_parser("aggregate")
    agg.add_argument("--spec", type=Path, required=True)
    agg.add_argument("--inputs", type=Path, required=True)
    agg.add_argument("--output", type=Path, required=True)
    agg.set_defaults(func=aggregate_command)
    a = p.parse_args()
    return a.func(a)


if __name__ == "__main__":
    raise SystemExit(main())
