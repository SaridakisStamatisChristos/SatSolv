#!/usr/bin/env python3
"""Independent original-CNF SAT model / UNSAT DRAT qualification of opt-in BVA."""
import argparse
import itertools
import json
import random
import subprocess
import tempfile
from pathlib import Path

import benchmark


def truth(n, clauses):
    return any(all(any((literal > 0) == bits[abs(literal) - 1] for literal in clause)
                   for clause in clauses)
               for bits in itertools.product((False, True), repeat=n))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--satsolv", type=Path, required=True)
    p.add_argument("--drat-trim", type=Path, required=True)
    p.add_argument("--cases", type=int, default=80)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if args.cases <= 0:
        p.error("--cases must be positive")
    rng = random.Random(0xB1C11A)
    binary = args.satsolv.resolve()
    verifier = args.drat_trim.resolve()
    checker = Path(__file__).resolve().parent / "check_model.py"
    observations = []
    with tempfile.TemporaryDirectory() as tmp:
        temp = Path(tmp)
        for index in range(args.cases):
            n = rng.randint(5, 7)
            neighbors = sorted(rng.sample(list(range(3, n + 1)), 3))
            clauses = [(a, b) for b in neighbors for a in (1, 2)]
            for _ in range(rng.randint(0, 13)):
                v = rng.randint(1, n)
                clauses.append((v if rng.randrange(2) else -v,))
            for _ in range(rng.randint(0, 8)):
                a, b = rng.sample(range(1, n + 1), 2)
                clauses.append((a if rng.randrange(2) else -a,
                                b if rng.randrange(2) else -b))
            expected = "SAT" if truth(n, clauses) else "UNSAT"
            cnf = temp / f"case-{index}.cnf"
            cnf.write_text(f"p cnf {n} {len(clauses)}\n"
                           + "".join(" ".join(map(str, row)) + " 0\n" for row in clauses))
            for arm, opts in (
                ("control", ()),
                ("candidate", ("--experimental-bva",)),
                ("zero_budget", ("--experimental-bva", "--bva-budget", "0")),
            ):
                proof = temp / "result.drat"
                stats = temp / "stats.json"
                proof.unlink(missing_ok=True)
                stats.unlink(missing_ok=True)
                command = [str(binary), str(cnf), "--proof", str(proof),
                           "--stats-json", str(stats), *opts]
                proc = subprocess.run(command, capture_output=True, text=True, timeout=15)
                result = benchmark.classify(proc.stdout)
                if result != expected or proc.returncode != (10 if result == "SAT" else 20):
                    raise RuntimeError(f"{index} {arm}: result/exit mismatch: {proc.returncode} {proc.stdout[-500:]} {proc.stderr[-500:]}")
                if not stats.exists():
                    raise RuntimeError(f"{index} {arm}: missing stats JSON")
                counters = json.loads(stats.read_text())
                if arm == "candidate" and counters.get("bva_variables_added", 0) < 1:
                    raise RuntimeError(f"{index}: BVA unexpectedly did not execute")
                if arm != "candidate" and counters.get("bva_variables_added", 0) != 0:
                    raise RuntimeError(f"{index} {arm}: BVA unexpectedly enabled")
                if result == "SAT":
                    output = temp / "model.out"
                    output.write_text(proc.stdout)
                    verified, details = benchmark.verify_sat(checker, cnf, proc.stdout, temp)
                else:
                    verified, details = benchmark.verify_unsat(verifier, cnf, proof, 30)
                if not verified:
                    raise RuntimeError(f"{index} {arm}: independent verification failed: {details}")
                observations.append({"case": index, "arm": arm, "truth": expected,
                                     "verified": verified, "extensions": counters.get("bva_variables_added"),
                                     "proof_bytes": proof.stat().st_size if proof.exists() else 0})
            if index % 10 == 0:
                print(f"verified case {index}/{args.cases}", flush=True)
    report = {
        "status": "PASS",
        "cases": args.cases,
        "certified_observations": len(observations),
        "model_oracle": "independent original-CNF checker",
        "unsat_oracle": "pinned external DRAT checker",
        "arm_identity": ["control", "candidate", "zero_budget"],
        "observations": observations,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {len(observations)} original-formula SAT/UNSAT certifications")


if __name__ == "__main__":
    main()
