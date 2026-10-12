#!/usr/bin/env python3
"""Differential logical-result comparison against MiniSat and Kissat."""
from __future__ import annotations
import argparse, random, subprocess, tempfile
from pathlib import Path


def write_formula(path, n, clauses):
    with path.open('w') as f:
        f.write(f'p cnf {n} {len(clauses)}\n')
        for c in clauses: f.write(' '.join(map(str,c))+' 0\n')


def parse_result(text):
    u=text.upper()
    if 'UNSATISFIABLE' in u or '\nUNSAT\n' in u or u.strip().endswith('UNSAT'):
        return 'UNSAT'
    if 'SATISFIABLE' in u or '\nSAT\n' in u or u.strip().endswith('SAT'):
        return 'SAT'
    return None


def run(cmd, timeout=5):
    p=subprocess.run(cmd,capture_output=True,text=True,timeout=timeout)
    return parse_result(p.stdout+'\n'+p.stderr)


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--satsolv',required=True,type=Path)
    ap.add_argument('--minisat',required=True,type=Path)
    ap.add_argument('--kissat',required=True,type=Path)
    ap.add_argument('--cases',type=int,default=200)
    a=ap.parse_args(); rng=random.Random(0xD1FF3)
    with tempfile.TemporaryDirectory() as td:
        td=Path(td)
        for i in range(a.cases):
            n=rng.randint(1,10); clauses=[]
            for _ in range(rng.randint(0,35)):
                k=rng.randint(1,min(5,n))
                clauses.append(tuple(rng.choice((-1,1))*rng.randint(1,n) for _ in range(k)))
            cnf=td/f'{i}.cnf'; write_formula(cnf,n,clauses)
            sr=run([str(a.satsolv.resolve()),str(cnf)])
            kr=run([str(a.kissat.resolve()),str(cnf)])
            mr=run([str(a.minisat.resolve()),str(cnf)])
            if None in (sr,mr,kr): raise AssertionError(f'case {i}: unparsed result satsolv={sr} minisat={mr} kissat={kr}')
            if not (sr==mr==kr): raise AssertionError(f'case {i}: mismatch satsolv={sr} minisat={mr} kissat={kr}')
    print(f'PASS: {a.cases} formulas agree across SatSolv, MiniSat, Kissat')
if __name__=='__main__': main()
