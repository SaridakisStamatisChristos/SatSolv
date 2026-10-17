#!/usr/bin/env python3
"""Proof-checked Kissat mechanism ablations plus conservative read-only XOR detection.

Diagnostic only: no SatSolv solver policy is modified or adopted by this experiment.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import tempfile
from collections import defaultdict
from pathlib import Path

import benchmark

# Three variables have four assignments of each parity. Each exact ternary
# clause excludes one assignment, according to the negated literals.
EVEN = sum(1 << i for i in range(8) if i.bit_count() % 2 == 0)
ODD = sum(1 << i for i in range(8) if i.bit_count() % 2 == 1)

ARMS = (
    ("baseline", ()),
    ("no_congruence_xors", ("--no-congruencexors",)),
    ("no_congruence", ("--no-congruence",)),
    ("no_elimination", ("--no-eliminate",)),
    ("no_factoring", ("--no-factor",)),
    ("no_sweeping", ("--no-sweep",)),
    ("no_preprocessing", ("--no-preprocess",)),
)


def clauses_in_dimacs(path: Path):
    """Strict streaming DIMACS reader, allowing clauses to span lines."""
    declared = None
    seen = 0
    pending = []
    with path.open("r", encoding="ascii") as stream:
        for line in stream:
            line = line.strip()
            if not line or line.startswith("c") or line.startswith("%"):
                continue
            if line.startswith("p"):
                fields = line.split()
                if declared is not None or len(fields) != 4 or fields[1] != "cnf":
                    raise ValueError("invalid/duplicate DIMACS header")
                nvars, nclauses = map(int, fields[2:])
                if nvars < 0 or nclauses < 0:
                    raise ValueError("negative DIMACS size")
                declared = (nvars, nclauses)
                continue
            if declared is None:
                raise ValueError("clause before DIMACS header")
            for field in line.split():
                literal = int(field)
                if literal == 0:
                    seen += 1
                    yield tuple(pending)
                    pending.clear()
                else:
                    if abs(literal) > declared[0]:
                        raise ValueError("literal outside DIMACS variable range")
                    pending.append(literal)
    if declared is None or pending or seen != declared[1]:
        raise ValueError("incomplete DIMACS or clause-count mismatch")


def scan_xor_quartets(cnf: Path) -> dict:
    masks = defaultdict(int)
    clause_count = 0
    ternary = 0
    for clause in clauses_in_dimacs(cnf):
        clause_count += 1
        if len(clause) != 3 or len({abs(lit) for lit in clause}) != 3:
            continue
        ternary += 1
        triple = tuple(sorted(abs(lit) for lit in clause))
        negated = {abs(lit) for lit in clause if lit < 0}
        forbidden = sum((1 << index) for index, var in enumerate(triple) if var in negated)
        masks[triple] |= 1 << forbidden
    # Never infer an equation from 1-3 clauses or from mixed/contradictory
    # parity patterns. Entire four-clause quartets must be present exactly.
    xor_zero = 0
    xor_one = 0
    ambiguous = 0
    for present in masks.values():
        if present == EVEN:
            xor_one += 1  # even assignments forbidden: XOR = 1.
        elif present == ODD:
            xor_zero += 1  # odd assignments forbidden: XOR = 0.
        elif present & EVEN == EVEN or present & ODD == ODD:
            ambiguous += 1
    return {
        "clauses": clause_count,
        "ternary_clauses": ternary,
        "distinct_ternary_triples": len(masks),
        "exact_xor_zero_quartets": xor_zero,
        "exact_xor_one_quartets": xor_one,
        "exact_xor_quartets": xor_zero + xor_one,
        "mixed_complete_parity_patterns_excluded": ambiguous,
        "scope": "exact complete 3-variable 4-clause gadgets only; not arbitrary XOR detection",
    }


def required_result(item: dict, record: dict) -> str | None:
    """Return an integrity failure, not an optimization conclusion."""
    if record["timed_out"]:
        return None
    if record["crashed"] or record["exit_code"] not in (0, 10, 20):
        return "solver crashed or returned an unsupported exit code"
    if record["result"] in ("SAT", "UNSAT"):
        if record["exit_code"] != (10 if record["result"] == "SAT" else 20):
            return "inconsistent SAT Competition exit code"
        if record["result"] != item["expected"]:
            return "solver contradicted pinned expected result"
        if not record["verified"]:
            return "definitive result not independently certified"
    elif record["exit_code"] == 0:
        return "Kissat returned UNKNOWN without exhausting external budget"
    else:
        return "unrecognized solver output"
    return None


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", type=Path, required=True)
    ap.add_argument("--root", type=Path, required=True)
    ap.add_argument("--kissat", type=Path, required=True)
    ap.add_argument("--drat-trim", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--instance", default="or_randxor_k3_n520_m520.cnf")
    ap.add_argument("--time-limit", type=float, default=60.0)
    ap.add_argument("--verification-time-limit", type=float, default=300.0)
    ap.add_argument("--memory-gib", type=float, default=4.0)
    args = ap.parse_args()
    for budget in (args.time_limit, args.verification_time_limit, args.memory_gib):
        if not math.isfinite(budget) or budget <= 0:
            ap.error("all budgets must be finite and positive")
    manifest = json.loads(args.manifest.read_text())
    matches = [row for row in manifest["instances"] if row["name"] == args.instance]
    if len(matches) != 1:
        ap.error("instance missing or repeated in pinned manifest")
    item = matches[0]
    cnf = (args.root / item["path"]).resolve()
    if benchmark.sha256(cnf) != item["sha256"]:
        raise SystemExit("CNF digest differs from frozen manifest")
    kissat = args.kissat.resolve()
    verifier = args.drat_trim.resolve()
    out = {
        "schema": 1, "purpose": "mechanism fingerprint only; not a superiority claim",
        "harness_commit": benchmark.git_commit(Path(".")),
        "manifest_sha256": benchmark.sha256(args.manifest),
        "kissat_sha256": benchmark.sha256(kissat),
        "drat_trim_sha256": benchmark.sha256(verifier),
        "environment": {"platform": platform.platform(), "time_limit": args.time_limit,
                        "proof_limit": args.verification_time_limit, "memory_gib": args.memory_gib},
        "instance": item["name"], "instance_sha256": item["sha256"],
        "expected": item["expected"], "xor_detection": scan_xor_quartets(cnf),
        "runs": [], "integrity_failures": [],
        "interpretation": "one-at-a-time ablations are exploratory; they may interact",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    def save():
        args.output.write_text(json.dumps(out, indent=2) + "\n")
    save()
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        checker = Path(__file__).resolve().parent / "check_model.py"
        for name, options in ARMS:
            proof = work / "candidate.drat"
            proof.unlink(missing_ok=True)
            command = [str(kissat), "--seed=1", "--statistics", *options, str(cnf), str(proof)]
            raw = benchmark.run_cmd(command, args.time_limit, int(args.memory_gib * (1024**3)))
            status = "TIMEOUT" if raw["timed_out"] else benchmark.classify(raw["stdout"])
            verified = False
            verification = {"status": "not_required"}
            if status == "SAT":
                verified, verification = benchmark.verify_sat(checker, cnf, raw["stdout"], work)
            elif status == "UNSAT":
                verified, verification = benchmark.verify_unsat(verifier, cnf, proof,
                                                                args.verification_time_limit)
            record = {
                "arm": name, "flags": list(options), "command": command,
                "result": status, "exit_code": raw["exit_code"], "elapsed": raw["elapsed"],
                "timed_out": raw["timed_out"], "crashed": raw["crashed"],
                "verified": verified, "verification": verification,
                "proof_bytes": proof.stat().st_size if proof.exists() else 0,
                "stdout_tail": raw["stdout"][-5000:], "stderr_tail": raw["stderr"][-2500:],
                "statistics_relevant": [line for line in raw["stdout"].splitlines()
                    if re.search(r"xor|congruence|eliminat|sweep|factor|conflicts", line, re.I)][-35:],
            }
            out["runs"].append(record)
            failure = required_result(item, record)
            if failure:
                out["integrity_failures"].append({"arm": name, "reason": failure})
            save()
            print(f"{name}: {status} elapsed={raw['elapsed']:.3f}s verified={verified}"
                  + (f" INTEGRITY FAILURE: {failure}" if failure else ""), flush=True)
            if failure:
                raise SystemExit("mechanism screen integrity failure, raw evidence retained")
    print("screen completed; results are hypotheses, not adoption evidence", flush=True)


if __name__ == "__main__":
    main()
