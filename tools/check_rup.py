#!/usr/bin/env python3
"""Independent RUP checker for SatSolv's proof subset.

SatSolv intentionally emits only RUP additions (a valid DRAT subset) and no deletions.
This checker is a development oracle; acceptance still uses external drat-trim.
"""
from __future__ import annotations
import argparse
from pathlib import Path


def parse_cnf(path: Path):
    clauses=[]; cur=[]
    for raw in path.read_text().splitlines():
        s=raw.strip()
        if not s or s.startswith('c') or s.startswith('p'): continue
        for t in s.split():
            x=int(t)
            if x==0:
                uniq=set(cur)
                if any(-lit in uniq for lit in uniq):
                    pass
                else:
                    clauses.append(tuple(sorted(uniq, key=lambda lit: (abs(lit), lit))))
                cur=[]
            else: cur.append(x)
    if cur: raise ValueError("unterminated CNF clause")
    return clauses


def parse_proof(path: Path):
    clauses=[]; cur=[]
    for raw in path.read_text().splitlines():
        s=raw.strip()
        if not s or s.startswith('c'): continue
        if s.startswith('d '):
            raise ValueError("development RUP checker does not support deletions")
        for t in s.split():
            x=int(t)
            if x==0:
                uniq=set(cur)
                if any(-lit in uniq for lit in uniq):
                    pass
                else:
                    clauses.append(tuple(sorted(uniq, key=lambda lit: (abs(lit), lit))))
                cur=[]
            else: cur.append(x)
    if cur: raise ValueError("unterminated proof clause")
    return clauses


def rup(db, clause):
    assign={}
    for lit in clause:
        v=abs(lit); val=lit < 0
        if v in assign and assign[v] != val:
            return True
        assign[v]=val
    changed=True
    while changed:
        changed=False
        for c in db:
            sat=False; un=[]
            for lit in c:
                v=abs(lit)
                if v not in assign: un.append(lit)
                elif (lit>0) == assign[v]: sat=True; break
            if sat: continue
            if not un: return True
            if len(un)==1:
                lit=un[0]; v=abs(lit); val=lit>0
                if v in assign:
                    if assign[v]!=val: return True
                else:
                    assign[v]=val; changed=True
    return False


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('cnf',type=Path); ap.add_argument('proof',type=Path); a=ap.parse_args()
    db=list(parse_cnf(a.cnf)); proof=parse_proof(a.proof)
    if not proof:
        print('FAIL: empty proof'); return 1
    for i,c in enumerate(proof,1):
        if not rup(db,c):
            print(f'FAIL: proof clause {i} is not RUP: {c}'); return 1
        db.append(c)
    if proof[-1] != ():
        print('FAIL: proof does not end with empty clause'); return 1
    print(f'PASS: {len(proof)} RUP additions accepted, including final empty clause')
    return 0
if __name__=='__main__': raise SystemExit(main())
