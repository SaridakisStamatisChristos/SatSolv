#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('pigeons',type=int); ap.add_argument('holes',type=int); ap.add_argument('output',type=Path); a=ap.parse_args()
    if a.pigeons <= 0 or a.holes <= 0: raise SystemExit('pigeons and holes must be positive')
    P,H=a.pigeons,a.holes
    def v(i,h): return i*H+h+1
    clauses=[]
    for i in range(P): clauses.append([v(i,h) for h in range(H)])
    for i in range(P):
        for x in range(H):
            for y in range(x+1,H): clauses.append([-v(i,x),-v(i,y)])
    for h in range(H):
        for i in range(P):
            for j in range(i+1,P): clauses.append([-v(i,h),-v(j,h)])
    with a.output.open('w') as f:
        f.write(f'c pigeonhole {P} pigeons into {H} holes\n')
        f.write(f'p cnf {P*H} {len(clauses)}\n')
        for c in clauses: f.write(' '.join(map(str,c))+' 0\n')
    print(f'wrote {a.output} with {P*H} variables and {len(clauses)} clauses')
if __name__=='__main__': main()
