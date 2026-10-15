#!/usr/bin/env python3
"""External same-binary validation of structural Kissat mechanisms.

This harness validates diagnostic candidates on a pre-registered SAT Competition
2026 holdout.  It intentionally does not run SatSolv: the question here is
whether disabling each candidate mechanism materially harms the exact pinned
Kissat executable outside the frozen 2023 diagnostic set.
"""
from __future__ import annotations

import argparse
import bz2
import gzip
import json
import lzma
import math
import shutil
import tempfile
import urllib.request
from pathlib import Path

import benchmark

UA = "SatSolv-structural-holdout/1.0"


def load_spec(path: Path) -> dict:
    spec = json.loads(path.read_text())
    if spec.get("schema_version") != 1:
        raise SystemExit("unsupported structural holdout schema")
    instances = spec.get("instances")
    arms = spec.get("kissat", {}).get("arms")
    if not isinstance(instances, list) or not instances:
        raise SystemExit("holdout has no instances")
    if not isinstance(arms, list) or not arms or arms[0].get("name") != "control":
        raise SystemExit("holdout has invalid arm list")
    ids = [x.get("id") for x in instances]
    if any(not x for x in ids) or len(ids) != len(set(ids)):
        raise SystemExit("missing or duplicate holdout ids")
    names = [a.get("name") for a in arms]
    if any(not x for x in names) or len(names) != len(set(names)):
        raise SystemExit("missing or duplicate arm names")
    if arms[0].get("flags") != []:
        raise SystemExit("control must have no option override")
    for item in instances:
        if item.get("expected") not in ("SAT", "UNSAT"):
            raise SystemExit(f"holdout truth missing for {item.get('id')}")
        if not item.get("gbd_hash") or not item.get("family") or not item.get("isohash2"):
            raise SystemExit(f"incomplete holdout identity for {item.get('id')}")
    return spec


def sha256(path: Path) -> str:
    return benchmark.sha256(path)


def download_gbd(item: dict, root: Path) -> tuple[Path, dict]:
    url = f"https://benchmark-database.de/file/{item['gbd_hash']}?context=cnf"
    source = root / f"{item['id']}.source"
    request = urllib.request.Request(url, headers={"User-Agent": UA, "Accept-Encoding": "identity"})
    with urllib.request.urlopen(request, timeout=300) as response, source.open("wb") as out:
        final_url = response.geturl()
        content_type = response.headers.get("Content-Type", "")
        disposition = response.headers.get("Content-Disposition", "")
        shutil.copyfileobj(response, out, length=1 << 20)

    with source.open("rb") as handle:
        magic = handle.read(6)
    cnf = root / f"{item['id']}.cnf"
    if magic.startswith(b"\xfd7zXZ\x00"):
        with lzma.open(source, "rb") as inp, cnf.open("wb") as out:
            shutil.copyfileobj(inp, out, length=1 << 20)
        encoding = "xz"
    elif magic.startswith(b"\x1f\x8b"):
        with gzip.open(source, "rb") as inp, cnf.open("wb") as out:
            shutil.copyfileobj(inp, out, length=1 << 20)
        encoding = "gzip"
    elif magic.startswith(b"BZh"):
        with bz2.open(source, "rb") as inp, cnf.open("wb") as out:
            shutil.copyfileobj(inp, out, length=1 << 20)
        encoding = "bzip2"
    else:
        shutil.copy2(source, cnf)
        encoding = "plain"

    with cnf.open("rb") as handle:
        prefix = handle.read(4 << 20)
    if not (prefix.startswith(b"p cnf ") or b"\np cnf " in prefix):
        raise SystemExit(f"GBD payload for {item['id']} is not recognizable DIMACS CNF")

    return cnf, {
        "gbd_hash": item["gbd_hash"],
        "isohash2": item["isohash2"],
        "requested_url": url,
        "final_url": final_url,
        "content_type": content_type,
        "content_disposition": disposition,
        "encoding": encoding,
        "source_sha256": sha256(source),
        "cnf_sha256": sha256(cnf),
        "source_bytes": source.stat().st_size,
        "cnf_bytes": cnf.stat().st_size,
    }


def definitive(record: dict) -> bool:
    return record.get("result") in ("SAT", "UNSAT") and bool(record.get("verified"))


def validate_record(record: dict, expected: str) -> list[str]:
    errors: list[str] = []
    elapsed = record.get("elapsed")
    if record.get("crashed"):
        errors.append(f"{record['arm']}: crashed")
    if not isinstance(elapsed, (int, float)) or not math.isfinite(elapsed) or elapsed < 0:
        errors.append(f"{record['arm']}: invalid elapsed time")
    result = record.get("result")
    if result in ("SAT", "UNSAT"):
        if result != expected:
            errors.append(f"{record['arm']}: truth disagreement {result} != {expected}")
        if not record.get("verified"):
            errors.append(f"{record['arm']}: definitive result failed independent verification")
        if record.get("exit_code") != (10 if result == "SAT" else 20):
            errors.append(f"{record['arm']}: invalid definitive exit code")
    elif result == "TIMEOUT":
        if not record.get("timed_out"):
            errors.append(f"{record['arm']}: inconsistent timeout classification")
    else:
        errors.append(f"{record['arm']}: unsupported solver result {result!r}")
    return errors


def run_arm(binary: Path, verifier: Path, cnf: Path, arm: dict, expected: str,
            budgets: dict, work: Path) -> dict:
    name = arm["name"]
    proof = work / f"{name}.drat"
    proof.unlink(missing_ok=True)
    checker = Path(__file__).resolve().parent / "check_model.py"
    command = [str(binary), f"--seed={int(budgets['seed'])}", *arm.get("flags", []), str(cnf), str(proof)]
    raw = benchmark.run_cmd(command, float(budgets["solver_seconds"]), int(float(budgets["memory_gib"]) * 1024**3))
    result = "TIMEOUT" if raw["timed_out"] else benchmark.classify(raw["stdout"])
    verified = False
    verification = {"exit_code": None, "stdout": "", "stderr": "not run"}
    if result == "SAT":
        verified, verification = benchmark.verify_sat(checker, cnf, raw["stdout"], work)
    elif result == "UNSAT":
        verified, verification = benchmark.verify_unsat(verifier, cnf, proof, float(budgets["proof_seconds"]))
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
        "binary_sha256": sha256(binary),
    })
    record["errors"] = validate_record(record, expected)
    return record


def classify_effect(control: dict, ablated: dict, threshold: float) -> dict:
    c = definitive(control)
    a = definitive(ablated)
    ratio = None
    if c and a:
        if control["result"] != ablated["result"]:
            return {"classification": "INTEGRITY_CONTRADICTION", "ratio_ablated_over_control": None}
        ratio = float(ablated["elapsed"]) / max(float(control["elapsed"]), 1e-9)
        if ratio >= threshold:
            cls = "MATERIAL_DEGRADATION"
        elif ratio <= 1.0 / threshold:
            cls = "MATERIAL_REVERSE"
        else:
            cls = "NEUTRAL"
    elif c and not a:
        cls = "MATERIAL_DEGRADATION"
    elif a and not c:
        cls = "MATERIAL_REVERSE"
    else:
        cls = "UNINFORMATIVE"
    return {"classification": cls, "ratio_ablated_over_control": ratio}


def case_command(args: argparse.Namespace) -> int:
    spec = load_spec(args.spec)
    item = next((x for x in spec["instances"] if x["id"] == args.case_id), None)
    if item is None:
        raise SystemExit(f"unregistered holdout case: {args.case_id}")
    binary = args.kissat.resolve()
    verifier = args.drat_trim.resolve()
    if not binary.is_file() or not verifier.is_file():
        raise SystemExit("missing pinned comparator or verifier")
    budgets = spec["budgets"]
    if any(not math.isfinite(float(budgets[k])) or float(budgets[k]) <= 0 for k in ("solver_seconds", "proof_seconds", "memory_gib")):
        raise SystemExit("invalid holdout budget")

    arms = list(spec["kissat"]["arms"])
    # Deterministic rotation avoids always giving one arm the same thermal/order position.
    offset = spec["instances"].index(item) % len(arms)
    order = arms[offset:] + arms[:offset]
    errors: list[str] = []
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        cnf, identity = download_gbd(item, root)
        records: dict[str, dict] = {}
        for arm in order:
            record = run_arm(binary, verifier, cnf, arm, item["expected"], budgets, root)
            records[arm["name"]] = record
            errors.extend(record["errors"])
            print(item["id"], arm["name"], record["result"], round(record["elapsed"], 3), "verified", record["verified"], flush=True)

    digests = {r["binary_sha256"] for r in records.values()}
    if len(digests) != 1:
        errors.append("arms did not use one exact Kissat executable")
    control = records["control"]
    effects = {}
    for arm in spec["kissat"]["arms"]:
        if arm["name"] == "control":
            continue
        e = classify_effect(control, records[arm["name"]], float(spec["decision_rule"]["material_ratio"]))
        effects[arm["name"]] = e
        if e["classification"] == "INTEGRITY_CONTRADICTION":
            errors.append(f"{arm['name']}: verified truth contradiction")

    doc = {
        "case": item,
        "selection_source": spec["selection_source"],
        "kissat_commit": spec["kissat"]["commit"],
        "budgets": budgets,
        "input_identity": identity,
        "execution_order": [a["name"] for a in order],
        "records": records,
        "effects": effects,
        "control_solved": definitive(control),
        "integrity_passed": not errors,
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(doc, indent=2) + "\n")
    print(json.dumps({"case": item["id"], "family": item["family"], "control_solved": definitive(control), "effects": effects, "integrity_passed": not errors}, indent=2))
    return 0 if not errors else 2


def aggregate_command(args: argparse.Namespace) -> int:
    spec = load_spec(args.spec)
    expected_ids = {x["id"] for x in spec["instances"]}
    docs = []
    for path in sorted(args.inputs.rglob("*.json")):
        try:
            doc = json.loads(path.read_text())
        except (json.JSONDecodeError, OSError):
            continue
        if isinstance(doc, dict) and isinstance(doc.get("case"), dict) and doc["case"].get("id") in expected_ids:
            docs.append(doc)
    by_id = {d["case"]["id"]: d for d in docs}
    errors: list[str] = []
    missing = sorted(expected_ids - set(by_id))
    if missing:
        errors.append("missing holdout evidence: " + ", ".join(missing))
    if len(docs) != len(by_id):
        errors.append("duplicate holdout evidence")
    for case_id, doc in sorted(by_id.items()):
        if not doc.get("integrity_passed"):
            errors.append(f"{case_id}: per-case integrity failed")

    control_solves = [case_id for case_id, doc in by_id.items() if doc.get("control_solved")]
    rule = spec["decision_rule"]
    mechanism_rows = {}
    validated = []
    for arm in spec["kissat"]["arms"]:
        name = arm["name"]
        if name == "control":
            continue
        degradations, reversals, neutral, uninformative = [], [], [], []
        for item in spec["instances"]:
            case_id = item["id"]
            if case_id not in by_id:
                continue
            cls = by_id[case_id]["effects"][name]["classification"]
            bucket = {
                "MATERIAL_DEGRADATION": degradations,
                "MATERIAL_REVERSE": reversals,
                "NEUTRAL": neutral,
                "UNINFORMATIVE": uninformative,
            }.get(cls)
            if bucket is None:
                errors.append(f"{case_id}/{name}: invalid classification {cls}")
            else:
                bucket.append(case_id)
        families = sorted({by_id[c]["case"]["family"] for c in degradations if c in by_id})
        passes = (
            len(control_solves) >= int(rule["minimum_control_solves"])
            and len(degradations) >= int(rule["minimum_degradations"])
            and len(families) >= int(rule["minimum_degradation_families"])
            and (not rule["require_degradations_exceed_reversals"] or len(degradations) > len(reversals))
        )
        mechanism_rows[name] = {
            "mechanism": arm.get("mechanism"),
            "flags": arm.get("flags", []),
            "material_degradations": degradations,
            "degradation_families": families,
            "material_reversals": reversals,
            "neutral": neutral,
            "uninformative": uninformative,
            "external_validation_passed": passes,
        }
        if passes:
            validated.append(name)

    sufficient_coverage = len(control_solves) >= int(rule["minimum_control_solves"])
    if not errors and not sufficient_coverage:
        decision = "INSUFFICIENT_CONTROL_COVERAGE"
    elif errors:
        decision = "INVALID_EVIDENCE"
    elif validated:
        decision = "IMPLEMENTATION_EXPERIMENT_ELIGIBLE"
    else:
        decision = "NO_EXTERNAL_VALIDATION"

    summary = {
        "integrity_passed": not errors,
        "integrity_errors": errors,
        "case_count": len(by_id),
        "control_solved_count": len(control_solves),
        "control_solved_cases": sorted(control_solves),
        "minimum_control_solves": int(rule["minimum_control_solves"]),
        "sufficient_control_coverage": sufficient_coverage,
        "mechanisms": mechanism_rows,
        "externally_validated_candidates": validated,
        "decision": decision,
        "claim_boundary": "A passing mechanism becomes eligible only for an isolated SatSolv implementation experiment. It is not adopted, merged, or claimed superior from this result.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if not errors else 2


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    case = sub.add_parser("case")
    case.add_argument("--spec", type=Path, required=True)
    case.add_argument("--case-id", required=True)
    case.add_argument("--kissat", type=Path, required=True)
    case.add_argument("--drat-trim", type=Path, required=True)
    case.add_argument("--output", type=Path, required=True)
    case.set_defaults(func=case_command)
    aggregate = sub.add_parser("aggregate")
    aggregate.add_argument("--spec", type=Path, required=True)
    aggregate.add_argument("--inputs", type=Path, required=True)
    aggregate.add_argument("--output", type=Path, required=True)
    aggregate.set_defaults(func=aggregate_command)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
