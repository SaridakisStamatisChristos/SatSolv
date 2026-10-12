#!/usr/bin/env python3
from __future__ import annotations
import argparse, random, subprocess, tempfile
from pathlib import Path


def solve(binary, n, clauses, path):
    with path.open('w') as f:
        f.write(f'p cnf {n} {len(clauses)}\n')
        for c in clauses: f.write(' '.join(map(str,c))+' 0\n')
    p=subprocess.run([str(binary),str(path)],capture_output=True,text=True,timeout=3)
    if 's SATISFIABLE' in p.stdout: return 'SAT'
    if 's UNSATISFIABLE' in p.stdout: return 'UNSAT'
    raise AssertionError(f'no logical result: {p.stdout} {p.stderr}')


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('binary',type=Path); ap.add_argument('--cases',type=int,default=300); a=ap.parse_args()
    b=a.binary.resolve(); rng=random.Random(7319)
    with tempfile.TemporaryDirectory() as d:
        d=Path(d)
        for i in range(a.cases):
            n=rng.randint(1,7); clauses=[]
            for _ in range(rng.randint(0,20)):
                clauses.append(tuple(rng.choice((-1,1))*rng.randint(1,n) for _ in range(rng.randint(1,5))))
            base=solve(b,n,clauses,d/'a.cnf')
            shuffled=[list(c) for c in clauses]; rng.shuffle(shuffled)
            for c in shuffled: rng.shuffle(c)
            if solve(b,n,[tuple(c) for c in shuffled],d/'b.cnf') != base: raise AssertionError(f'permutation changed result case {i}')
            dup=list(clauses)
            if clauses: dup.append(rng.choice(clauses))
            if solve(b,n,dup,d/'c.cnf') != base: raise AssertionError(f'duplicate changed result case {i}')
            taut=list(clauses)+[(1,-1)]
            if solve(b,n,taut,d/'d.cnf') != base: raise AssertionError(f'tautology changed result case {i}')
            perm=list(range(n+1)); xs=list(range(1,n+1)); rng.shuffle(xs)
            for old,new in enumerate(xs,1): perm[old]=new
            renamed=[tuple((1 if l>0 else -1)*perm[abs(l)] for l in c) for c in clauses]
            if solve(b,n,renamed,d/'e.cnf') != base: raise AssertionError(f'rename changed result case {i}')
    print(f'PASS: {a.cases} metamorphic cases x4 transformations')
if __name__=='__main__': main()
