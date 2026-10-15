#!/usr/bin/env python3
"""Control-only SAT Competition 2026 qualification screen.

This stage observes exactly one solver configuration: pinned default Kissat.
It must not contain or execute feature-ablation flags.  The aggregate selects
cases only by pre-registered candidate order plus independent certification and
runtime margin, producing a proposed holdout that must be committed separately
before any ablation experiment is run.
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

UA = "SatSolv-holdout-control-screen/2.0"


def load_spec(path: Path) -> dict:
    spec = json.loads(path.read_text())
    if spec.get("schema_version") != 1:
        raise SystemExit("unsupported holdout-screen schema")
    candidates = spec.get("candidates")
    families = spec.get("families")
    if not isinstance(candidates, list) or not candidates:
        raise SystemExit("screen candidate pool is empty")
    if not isinstance(families, list) or not families:
        raise SystemExit("screen family list is empty")
    ids = [x.get("id") for x in candidates]
    if any(not x for x in ids) or len(ids) != len(set(ids)):
        raise SystemExit("missing or duplicate candidate ids")
    family_names = [x.get("name") for x in families]
    if any(not x for x in family_names) or len(family_names) != len(set(family_names)):
        raise SystemExit("missing or duplicate families")
    rule = spec["selection_rule"]
    per_family = int(rule["per_family_candidates"])
    for family in families:
        rows = [x for x in candidates if x.get("family") == family["name"]]
        if len(rows) != per_family:
            raise SystemExit(f"family {family['name']} has {len(rows)} candidates, expected {per_family}")
        if any(x.get("expected") != family["expected"] for x in rows):
            raise SystemExit(f"family truth mismatch for {family['name']}")
    for item in candidates:
        if item.get("expected") not in ("SAT", "UNSAT"):
            raise SystemExit(f"candidate truth is not fixed for {item.get('id')}")
        if not item.get("gbd_hash") or not item.get("isohash2"):
            raise SystemExit(f"candidate identity incomplete for {item.get('id')}")
    # This script is intentionally incapable of reading ablation-arm definitions.
    forbidden = ("arms", "ablation", "no_probe", "no_congruence", "no_factor")
    encoded = json.dumps(spec).lower()
    if any(token in encoded for token in forbidden):
        raise SystemExit("control screen spec contains forbidden ablation material")
    return spec


def download_gbd(item: dict, root: Path) -> tuple[Path, dict]:
    url = f"https://benchmark-database.de/file/{item['gbd_hash']}?context=cnf"
    source = root / f"{item['id']}.source"
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept-Encoding": "identity"})
    with urllib.request.urlopen(req, timeout=300) as response, source.open("wb") as out:
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
        "source_sha256": benchmark.sha256(source),
        "cnf_sha256": benchmark.sha256(cnf),
        "source_bytes": source.stat().st_size,
        "cnf_bytes": cnf.stat().st_size,
    }


def screen_case(args: argparse.Namespace) -> int:
    spec = load_spec(args.spec)
    item = next((x for x in spec["candidates"] if x["id"] == args.case_id), None)
    if item is None:
        raise SystemExit(f"case not in pre-registered candidate pool: {args.case_id}")
    binary = args.kissat.resolve()
    verifier = args.drat_trim.resolve()
    if not binary.is_file() or not verifier.is_file():
        raise SystemExit("missing pinned Kissat or drat-trim")
    budgets = spec["budgets"]
    solver_seconds = float(budgets["screen_solver_seconds"])
    proof_seconds = float(budgets["proof_seconds"])
    memory_gib = float(budgets["memory_gib"])
    seed = int(budgets["seed"])
    if any(not math.isfinite(v) or v <= 0 for v in (solver_seconds, proof_seconds, memory_gib)):
        raise SystemExit("screen budgets must be finite and positive")

    errors: list[str] = []
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        cnf, identity = download_gbd(item, root)
        proof = root / "control.drat"
        checker = Path(__file__).resolve().parent / "check_model.py"
        # IMPORTANT: default Kissat only. No feature flags are accepted by the CLI.
        command = [str(binary), f"--seed={seed}", str(cnf), str(proof)]
        raw = benchmark.run_cmd(command, solver_seconds, int(memory_gib * 1024**3))
        result = "TIMEOUT" if raw["timed_out"] else benchmark.classify(raw["stdout"])
        verified = False
        verification = {"exit_code": None, "stdout": "", "stderr": "not run"}
        if result == "SAT":
            verified, verification = benchmark.verify_sat(checker, cnf, raw["stdout"], root)
        elif result == "UNSAT":
            verified, verification = benchmark.verify_unsat(verifier, cnf, proof, proof_seconds)

    if raw.get("crashed"):
        errors.append("default Kissat crashed")
    if not math.isfinite(float(raw.get("elapsed", math.nan))) or float(raw.get("elapsed", -1)) < 0:
        errors.append("invalid elapsed time")
    if result in ("SAT", "UNSAT"):
        if result != item["expected"]:
            errors.append(f"truth disagreement: {result} != {item['expected']}")
        if not verified:
            errors.append("definitive result failed independent certification")
        if raw.get("exit_code") != (10 if result == "SAT" else 20):
            errors.append("invalid definitive exit code")
    elif result != "TIMEOUT":
        errors.append(f"unsupported control outcome {result!r}")

    eligible = not errors and result == item["expected"] and verified and not raw["timed_out"] and raw["elapsed"] <= solver_seconds
    doc = {
        "candidate": item,
        "selection_source": spec["selection_source"],
        "kissat_commit": spec["kissat_commit"],
        "binary_sha256": benchmark.sha256(binary),
        "budgets": budgets,
        "input_identity": identity,
        "control": {
            **raw,
            "result": result,
            "verified": verified,
            "verification": verification,
            "proof_bytes": proof.stat().st_size if proof.exists() else 0,
            "command": command,
        },
        "eligible": eligible,
        "integrity_passed": not errors,
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(doc, indent=2) + "\n")
    print(json.dumps({
        "case": item["id"],
        "family": item["family"],
        "expected": item["expected"],
        "result": result,
        "elapsed": raw["elapsed"],
        "verified": verified,
        "eligible": eligible,
        "integrity_passed": not errors,
    }, indent=2))
    # A timeout is a valid screen result, not a workflow failure. Integrity errors fail closed.
    return 0 if not errors else 2


def aggregate(args: argparse.Namespace) -> int:
    spec = load_spec(args.spec)
    expected_ids = {x["id"] for x in spec["candidates"]}
    docs = []
    for path in sorted(args.inputs.rglob("*.json")):
        try:
            doc = json.loads(path.read_text())
        except (json.JSONDecodeError, OSError):
            continue
        candidate = doc.get("candidate") if isinstance(doc, dict) else None
        if isinstance(candidate, dict) and candidate.get("id") in expected_ids:
            docs.append(doc)
    by_id = {d["candidate"]["id"]: d for d in docs}
    errors: list[str] = []
    missing = sorted(expected_ids - set(by_id))
    if missing:
        errors.append("missing screen evidence: " + ", ".join(missing))
    if len(docs) != len(by_id):
        errors.append("duplicate screen evidence")
    for case_id, doc in sorted(by_id.items()):
        if not doc.get("integrity_passed"):
            errors.append(f"{case_id}: integrity failure")

    take = int(spec["selection_rule"]["select_first_eligible_per_family"])
    selected = []
    family_summary = {}
    for family in spec["families"]:
        name = family["name"]
        ordered = [x for x in spec["candidates"] if x["family"] == name]
        eligible = [x for x in ordered if x["id"] in by_id and by_id[x["id"]].get("eligible")]
        chosen = eligible[:take]
        family_summary[name] = {
            "candidate_count": len(ordered),
            "eligible_count": len(eligible),
            "eligible_ids": [x["id"] for x in eligible],
            "selected_ids": [x["id"] for x in chosen],
            "sufficient": len(chosen) == take,
        }
        for item in chosen:
            evidence = by_id[item["id"]]
            selected.append({
                **item,
                "control_elapsed": evidence["control"]["elapsed"],
                "control_result": evidence["control"]["result"],
                "control_verified": evidence["control"]["verified"],
                "cnf_sha256": evidence["input_identity"]["cnf_sha256"],
                "source_sha256": evidence["input_identity"]["source_sha256"],
            })

    all_families_sufficient = all(row["sufficient"] for row in family_summary.values())
    required_cases = int(spec["selection_rule"]["required_selected_cases"])
    sat = sum(x["expected"] == "SAT" for x in selected)
    unsat = sum(x["expected"] == "UNSAT" for x in selected)
    target_balance = spec["selection_rule"]["expected_truth_balance"]
    balance_ok = sat == int(target_balance["SAT"]) and unsat == int(target_balance["UNSAT"])
    selection_passed = not errors and all_families_sufficient and len(selected) == required_cases and balance_ok
    decision = "FREEZE_SELECTED_HOLDOUT" if selection_passed else "SCREEN_INSUFFICIENT"

    output = {
        "integrity_passed": not errors,
        "integrity_errors": errors,
        "candidate_count": len(by_id),
        "family_summary": family_summary,
        "selected_count": len(selected),
        "selected_truth_balance": {"SAT": sat, "UNSAT": unsat},
        "selection_passed": selection_passed,
        "decision": decision,
        "selected": selected,
        "freeze_contract": "If decision is FREEZE_SELECTED_HOLDOUT, commit this exact selected list and identities in a separate branch before running any feature ablation. Do not replace cases based on subsequent ablation behavior.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(json.dumps(output, indent=2))
    return 0 if not errors else 2


def main() -> int:
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="command", required=True)
    case = sub.add_parser("case")
    case.add_argument("--spec", type=Path, required=True)
    case.add_argument("--case-id", required=True)
    case.add_argument("--kissat", type=Path, required=True)
    case.add_argument("--drat-trim", type=Path, required=True)
    case.add_argument("--output", type=Path, required=True)
    case.set_defaults(func=screen_case)
    agg = sub.add_parser("aggregate")
    agg.add_argument("--spec", type=Path, required=True)
    agg.add_argument("--inputs", type=Path, required=True)
    agg.add_argument("--output", type=Path, required=True)
    agg.set_defaults(func=aggregate)
    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
