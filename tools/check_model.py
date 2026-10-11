#!/usr/bin/env python3
"""Independent SAT witness checker for DIMACS CNF + SAT Competition-style output."""
from __future__ import annotations
import argparse
import sys
from pathlib import Path


def parse_cnf(path: Path):
    nvars = None
    nclauses = None
    clauses = []
    current = []
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        s = raw.strip()
        if not s or s.startswith("c"):
            continue
        if s.startswith("p"):
            if nvars is not None:
                raise ValueError(f"duplicate header at line {lineno}")
            parts = s.split()
            if len(parts) != 4 or parts[:2] != ["p", "cnf"]:
                raise ValueError(f"bad header at line {lineno}")
            nvars, nclauses = map(int, parts[2:])
            if nvars < 0 or nclauses < 0:
                raise ValueError("negative header count")
            continue
        if nvars is None:
            raise ValueError(f"clause before header at line {lineno}")
        for token in s.split():
            lit = int(token)
            if lit == 0:
                clauses.append(tuple(current))
                current.clear()
            else:
                if abs(lit) < 1 or abs(lit) > nvars:
                    raise ValueError(f"literal {lit} out of range")
                current.append(lit)
    if nvars is None:
        raise ValueError("missing header")
    if current:
        raise ValueError("unterminated clause")
    if len(clauses) != nclauses:
        raise ValueError(f"clause count mismatch: {len(clauses)} != {nclauses}")
    return nvars, clauses


def parse_model(path: Path):
    status = None
    lits = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if not s or s.startswith("c"):
            continue
        if s.startswith("s "):
            status = s[2:].strip()
        elif s.startswith("v"):
            for token in s[1:].split():
                lit = int(token)
                if lit != 0:
                    lits.append(lit)
    return status, lits


def check(nvars, clauses, model):
    values = {}
    for lit in model:
        v = abs(lit)
        if v < 1 or v > nvars:
            return False, f"model variable {v} out of range"
        value = lit > 0
        if v in values and values[v] != value:
            return False, f"contradictory assignments for variable {v}"
        values[v] = value
    for idx, clause in enumerate(clauses, 1):
        if not any((lit > 0) == values.get(abs(lit), False) for lit in clause if abs(lit) in values):
            return False, f"clause {idx} is not satisfied: {' '.join(map(str, clause))} 0"
    return True, "model satisfies every original clause"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cnf", type=Path)
    ap.add_argument("solver_output", type=Path)
    args = ap.parse_args()
    try:
        nvars, clauses = parse_cnf(args.cnf)
        status, model = parse_model(args.solver_output)
        if status != "SATISFIABLE":
            print(f"FAIL: solver output status is {status!r}, expected SATISFIABLE", file=sys.stderr)
            return 2
        ok, message = check(nvars, clauses, model)
        print(("PASS: " if ok else "FAIL: ") + message)
        return 0 if ok else 1
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

if __name__ == "__main__":
    raise SystemExit(main())
