#!/usr/bin/env python3
"""Causal Kissat XOR-congruence ablation on a pre-registered external holdout.

The per-case runner downloads one content-addressed GBD instance, runs one
pinned Kissat executable twice (default and --congruencexors=0), and verifies
every definitive result independently.  The aggregate command applies the
pre-registered family-aware decision rule.  A negative mechanism result is a
valid experiment outcome; integrity or certification failures are not.
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

UA = "SatSolv-XOR-ablation/1.0"


def load_spec(path: Path) -> dict:
    data = json.loads(path.read_text())
    if data.get("schema_version") != 1:
        raise SystemExit("unsupported XOR ablation spec schema")
    instances = data.get("instances")
    if not isinstance(instances, list) or not instances:
        raise SystemExit("XOR ablation spec has no instances")
    ids = [item.get("id") for item in instances]
    if any(not value for value in ids) or len(set(ids)) != len(ids):
        raise SystemExit("XOR ablation instance ids are missing or duplicated")
    for item in instances:
        if item.get("expected") not in ("SAT", "UNSAT", "UNKNOWN"):
            raise SystemExit(f"invalid expected result for {item.get('id')}")
        if not item.get("gbd_hash") or not item.get("family"):
            raise SystemExit(f"incomplete holdout identity for {item.get('id')}")
    return data


def sha256(path: Path) -> str:
    return benchmark.sha256(path)


def download_gbd(item: dict, root: Path) -> tuple[Path, dict]:
    gbd_hash = item["gbd_hash"]
    url = f"https://benchmark-database.de/file/{gbd_hash}?context=cnf"
    source = root / f"{item['id']}.source"
    request = urllib.request.Request(
        url,
        headers={"User-Agent": UA, "Accept-Encoding": "identity"},
    )
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

    identity = {
        "gbd_hash": gbd_hash,
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
    return cnf, identity


def definitive(record: dict) -> bool:
    return record["result"] in ("SAT", "UNSAT") and bool(record["verified"])


def run_arm(
    *,
    name: str,
    binary: Path,
    cnf: Path,
    flags: list[str],
    seed: int,
    time_limit: float,
    proof_limit: float,
    memory_gib: float,
    work: Path,
) -> dict:
    proof = work / f"{name}.drat"
    checker = Path(__file__).resolve().parent / "check_model.py"
    command = [str(binary), f"--seed={seed}", *flags, str(cnf), str(proof)]
    raw = benchmark.run_cmd(command, time_limit, int(memory_gib * 1024**3))
    result = "TIMEOUT" if raw["timed_out"] else benchmark.classify(raw["stdout"])
    verified = False
    verification = {"exit_code": None, "stdout": "", "stderr": "not run"}
    if result == "SAT":
        verified, verification = benchmark.verify_sat(checker, cnf, raw["stdout"], work)
    elif result == "UNSAT":
        verified, verification = benchmark.verify_unsat(
            Path(run_arm.verifier), cnf, proof, proof_limit
        )

    record = dict(raw)
    record.update(
        {
            "arm": name,
            "flags": flags,
            "command": command,
            "result": result,
            "verified": verified,
            "verification": verification,
            "proof_bytes": proof.stat().st_size if proof.exists() else 0,
            "binary_sha256": sha256(binary),
        }
    )
    return record


# Set once by the case subcommand before run_arm is called.  Keeping the path
# outside arm arguments makes it impossible to accidentally use different
# proof checkers for the two causal arms.
run_arm.verifier = ""


def validate_arm(record: dict, expected: str) -> list[str]:
    errors: list[str] = []
    if record.get("crashed"):
        errors.append(f"{record['arm']}: crashed")
    elapsed = record.get("elapsed")
    if not isinstance(elapsed, (int, float)) or not math.isfinite(elapsed) or elapsed < 0:
        errors.append(f"{record['arm']}: invalid elapsed time")
    result = record.get("result")
    if result in ("SAT", "UNSAT"):
        if not record.get("verified"):
            errors.append(f"{record['arm']}: definitive result failed independent verification")
        expected_code = 10 if result == "SAT" else 20
        if record.get("exit_code") != expected_code:
            errors.append(f"{record['arm']}: invalid definitive exit code")
        if expected != "UNKNOWN" and result != expected:
            errors.append(f"{record['arm']}: disagrees with pre-registered truth {expected}")
    elif result == "TIMEOUT":
        if not record.get("timed_out"):
            errors.append(f"{record['arm']}: inconsistent timeout classification")
    elif result == "UNKNOWN":
        if record.get("timed_out"):
            errors.append(f"{record['arm']}: timeout mislabeled UNKNOWN")
    else:
        errors.append(f"{record['arm']}: unsupported solver outcome {result!r}")
    return errors


def classify_effect(control: dict, ablated: dict, material_ratio: float) -> dict:
    c_solved = definitive(control)
    a_solved = definitive(ablated)
    ratio = None
    if c_solved and a_solved:
        if control["result"] != ablated["result"]:
            return {
                "classification": "INTEGRITY_CONTRADICTION",
                "ratio_ablated_over_control": None,
            }
        denominator = max(float(control["elapsed"]), 1e-9)
        ratio = float(ablated["elapsed"]) / denominator
        if ratio >= material_ratio:
            classification = "MATERIAL_DEGRADATION"
        elif ratio <= 1.0 / material_ratio:
            classification = "MATERIAL_REVERSE"
        else:
            classification = "NEUTRAL"
    elif c_solved and not a_solved:
        classification = "MATERIAL_DEGRADATION"
    elif a_solved and not c_solved:
        classification = "MATERIAL_REVERSE"
    else:
        classification = "NEUTRAL"
    return {"classification": classification, "ratio_ablated_over_control": ratio}


def case_command(args: argparse.Namespace) -> int:
    spec = load_spec(args.spec)
    item = next((x for x in spec["instances"] if x["id"] == args.case_id), None)
    if item is None:
        raise SystemExit(f"unknown holdout case: {args.case_id}")
    budgets = spec["budgets"]
    time_limit = float(args.time_limit if args.time_limit is not None else budgets["solver_seconds"])
    proof_limit = float(
        args.verification_time_limit
        if args.verification_time_limit is not None
        else budgets["proof_seconds"]
    )
    memory_gib = float(args.memory_gib if args.memory_gib is not None else budgets["memory_gib"])
    seed = int(budgets["seed"])
    if any(not math.isfinite(v) or v <= 0 for v in (time_limit, proof_limit, memory_gib)):
        raise SystemExit("budgets must be finite and positive")

    binary = args.kissat.resolve()
    verifier = args.drat_trim.resolve()
    run_arm.verifier = str(verifier)
    if not binary.is_file() or not verifier.is_file():
        raise SystemExit("missing Kissat or drat-trim binary")

    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        cnf, identity = download_gbd(item, work)
        control = run_arm(
            name="control",
            binary=binary,
            cnf=cnf,
            flags=list(spec["kissat"]["control_flags"]),
            seed=seed,
            time_limit=time_limit,
            proof_limit=proof_limit,
            memory_gib=memory_gib,
            work=work,
        )
        ablated = run_arm(
            name="no_xor",
            binary=binary,
            cnf=cnf,
            flags=list(spec["kissat"]["ablation_flags"]),
            seed=seed,
            time_limit=time_limit,
            proof_limit=proof_limit,
            memory_gib=memory_gib,
            work=work,
        )

    errors = validate_arm(control, item["expected"]) + validate_arm(ablated, item["expected"])
    if control["binary_sha256"] != ablated["binary_sha256"]:
        errors.append("control and ablation did not use the same Kissat executable")
    effect = classify_effect(control, ablated, float(spec["decision_rule"]["material_ratio"]))
    if effect["classification"] == "INTEGRITY_CONTRADICTION":
        errors.append("independently verified arms disagree on SAT/UNSAT truth")

    document = {
        "case": item,
        "selection_source": spec["selection_source"],
        "kissat_commit": spec["kissat"]["commit"],
        "budgets": {
            "solver_seconds": time_limit,
            "proof_seconds": proof_limit,
            "memory_gib": memory_gib,
            "seed": seed,
        },
        "input_identity": identity,
        "arms": {"control": control, "no_xor": ablated},
        "effect": effect,
        "integrity_passed": not errors,
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")
    print(
        json.dumps(
            {
                "case": item["id"],
                "family": item["family"],
                "control": {"result": control["result"], "elapsed": control["elapsed"], "verified": control["verified"]},
                "no_xor": {"result": ablated["result"], "elapsed": ablated["elapsed"], "verified": ablated["verified"]},
                "effect": effect,
                "integrity_passed": not errors,
            },
            indent=2,
        )
    )
    return 0 if not errors else 2


def aggregate_command(args: argparse.Namespace) -> int:
    spec = load_spec(args.spec)
    expected_ids = {item["id"] for item in spec["instances"]}
    documents = []
    for path in sorted(args.inputs.rglob("*.json")):
        try:
            doc = json.loads(path.read_text())
        except json.JSONDecodeError:
            continue
        if isinstance(doc, dict) and isinstance(doc.get("case"), dict) and doc["case"].get("id") in expected_ids:
            documents.append(doc)

    by_id = {doc["case"]["id"]: doc for doc in documents}
    missing = sorted(expected_ids - set(by_id))
    duplicates = len(documents) != len(by_id)
    integrity_errors = []
    if missing:
        integrity_errors.append("missing case evidence: " + ", ".join(missing))
    if duplicates:
        integrity_errors.append("duplicate case evidence")
    for case_id, doc in sorted(by_id.items()):
        if not doc.get("integrity_passed"):
            integrity_errors.append(f"{case_id}: per-case integrity failed")

    degradations = []
    reversals = []
    neutral = []
    for case_id, doc in sorted(by_id.items()):
        classification = doc["effect"]["classification"]
        if classification == "MATERIAL_DEGRADATION":
            degradations.append(case_id)
        elif classification == "MATERIAL_REVERSE":
            reversals.append(case_id)
        elif classification == "NEUTRAL":
            neutral.append(case_id)
        else:
            integrity_errors.append(f"{case_id}: invalid effect classification {classification}")

    families = {by_id[case_id]["case"]["family"] for case_id in degradations if case_id in by_id}
    xor_degradations = [
        case_id
        for case_id in degradations
        if by_id[case_id]["case"]["family"] == "xor-shifting"
    ]
    rule = spec["decision_rule"]
    support = (
        not integrity_errors
        and len(degradations) >= int(rule["minimum_degradations"])
        and len(families) >= int(rule["minimum_degradation_families"])
        and (not rule["require_xor_shifting_degradation"] or bool(xor_degradations))
        and (
            not rule["require_degradations_exceed_reversals"]
            or len(degradations) > len(reversals)
        )
    )
    decision = "GO_BOUNDED_XOR_CONGRUENCE" if support else "NO_GO_XOR_CONGRUENCE"

    family_summary = {}
    for family in sorted({item["family"] for item in spec["instances"]}):
        ids = [item["id"] for item in spec["instances"] if item["family"] == family]
        family_summary[family] = {
            "cases": ids,
            "material_degradations": [i for i in ids if i in degradations],
            "material_reversals": [i for i in ids if i in reversals],
            "neutral": [i for i in ids if i in neutral],
        }

    summary = {
        "integrity_passed": not integrity_errors,
        "integrity_errors": integrity_errors,
        "case_count": len(by_id),
        "material_degradations": degradations,
        "material_reversals": reversals,
        "neutral": neutral,
        "degradation_families": sorted(families),
        "xor_shifting_degradations": xor_degradations,
        "family_summary": family_summary,
        "decision_rule": rule,
        "xor_mechanism_supported": support,
        "implementation_decision": decision,
        "claim_boundary": "This ablation can attribute a bounded mechanism on the named 2026 holdout; it does not establish SatSolv superiority or justify Gaussian elimination.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if not integrity_errors else 2


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    case = sub.add_parser("case")
    case.add_argument("--spec", type=Path, required=True)
    case.add_argument("--case-id", required=True)
    case.add_argument("--kissat", type=Path, required=True)
    case.add_argument("--drat-trim", type=Path, required=True)
    case.add_argument("--output", type=Path, required=True)
    case.add_argument("--time-limit", type=float)
    case.add_argument("--verification-time-limit", type=float)
    case.add_argument("--memory-gib", type=float)
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
