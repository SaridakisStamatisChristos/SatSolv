#!/usr/bin/env python3
"""Deterministic exhaustive/randomized classification and witness/proof validation."""
from __future__ import annotations
import argparse, itertools, random, subprocess, tempfile
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_model
import check_rup


def truth(nvars, clauses):
    for bits in itertools.product((False, True), repeat=nvars):
        if all(any((lit > 0) == bits[abs(lit)-1] for lit in c) for c in clauses):
            return True
    return False


def write_cnf(path, nvars, clauses):
    with path.open('w') as f:
        f.write(f'p cnf {nvars} {len(clauses)}\n')
        for c in clauses: f.write(' '.join(map(str,c)) + ' 0\n')


def run_one(binary: Path, nvars, clauses, tmp: Path, idx: int):
    cnf=tmp/f'f{idx}.cnf'; out=tmp/f'f{idx}.out'; proof=tmp/f'f{idx}.drat'
    write_cnf(cnf,nvars,clauses)
    p=subprocess.run([str(binary),str(cnf),'--proof',str(proof),'--seed','1'],text=True,capture_output=True,timeout=5)
    out.write_text(p.stdout)
    expected=truth(nvars,clauses)
    got_sat='s SATISFIABLE' in p.stdout
    got_unsat='s UNSATISFIABLE' in p.stdout
    if expected != got_sat or expected == got_unsat:
        raise AssertionError(f'misclassification idx={idx} expected={expected} rc={p.returncode} out={p.stdout} err={p.stderr}')
    if expected:
        nv, parsed_clauses = check_model.parse_cnf(cnf)
        status, model = check_model.parse_model(out)
        ok, message = check_model.check(nv, parsed_clauses, model)
        if status != 'SATISFIABLE' or not ok:
            raise AssertionError(f'independent model validation failed idx={idx}: {message}')
    else:
        db=list(check_rup.parse_cnf(cnf)); additions=check_rup.parse_proof(proof)
        if not additions or additions[-1] != ():
            raise AssertionError(f'independent RUP validation failed idx={idx}: missing final empty clause')
        for j,c in enumerate(additions,1):
            if not check_rup.rup(db,c):
                raise AssertionError(f'independent RUP validation failed idx={idx} proof_clause={j}: {c}')
            db.append(c)


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('binary',type=Path); ap.add_argument('--random',type=int,default=1000); a=ap.parse_args()
    binary=a.binary.resolve(); rng=random.Random(0x5A75017)
    idx=0
    with tempfile.TemporaryDirectory() as d:
        tmp=Path(d)
        universe=[(1,),(-1,),(2,),(-2,),(1,2),(1,-2),(-1,2),(-1,-2)]
        for mask in range(1 << len(universe)):
            clauses=[universe[i] for i in range(len(universe)) if mask & (1<<i)]
            run_one(binary,2,clauses,tmp,idx); idx+=1
        for _ in range(a.random):
            n=rng.randint(0,6); m=rng.randint(0,18); clauses=[]
            for _ in range(m):
                if n==0:
                    c=() if rng.random()<0.2 else ()
                else:
                    k=rng.randint(0,min(6,n+2)); c=tuple(rng.choice((-1,1))*rng.randint(1,n) for _ in range(k))
                clauses.append(c)
            run_one(binary,n,clauses,tmp,idx); idx+=1
    print(f'PASS: {idx} formulas classified correctly with independent SAT/RUP validation')
    return 0
if __name__=='__main__': raise SystemExit(main())
