#!/usr/bin/env python3
"""Independent RUP checker for SatSolv's proof subset.

SatSolv intentionally emits only RUP additions (a valid DRAT subset) and no deletions.
Both textual DRAT and binary DRAT are parsed independently here. This checker is a
local development oracle; acceptance still requires pinned external drat-trim.
"""
from __future__ import annotations
import argparse
from pathlib import Path


def _normalize_clause(cur):
    uniq=set(cur)
    if any(-lit in uniq for lit in uniq):
        return None
    return tuple(sorted(uniq, key=lambda lit: (abs(lit), lit)))


def parse_cnf(path: Path):
    clauses=[]; cur=[]
    for raw in path.read_text().splitlines():
        s=raw.strip()
        if not s or s.startswith('c') or s.startswith('p'): continue
        for t in s.split():
            x=int(t)
            if x==0:
                clause=_normalize_clause(cur)
                if clause is not None: clauses.append(clause)
                cur=[]
            else: cur.append(x)
    if cur: raise ValueError("unterminated CNF clause")
    return clauses


def _parse_text_proof(data: bytes):
    clauses=[]; cur=[]
    text=data.decode('ascii')
    for raw in text.splitlines():
        s=raw.strip()
        if not s or s.startswith('c'): continue
        if s.startswith('d '):
            raise ValueError("development RUP checker does not support deletions")
        for t in s.split():
            x=int(t)
            if x==0:
                clause=_normalize_clause(cur)
                if clause is not None: clauses.append(clause)
                cur=[]
            else: cur.append(x)
    if cur: raise ValueError("unterminated proof clause")
    return clauses


def _parse_binary_proof(data: bytes):
    """Parse binary DRAT additions using drat-trim's literal encoding.

    A clause starts with byte 'a' (addition) or 'd' (deletion), followed by
    unsigned 7-bit varints and a zero byte terminator. A literal code is
    2*var for positive literals and 2*var+1 for negative literals.
    SatSolv emits additions only, so deletions fail closed here.
    """
    clauses=[]
    pos=0
    n=len(data)
    while pos < n:
        op=data[pos]; pos+=1
        if op not in (ord('a'), ord('d')):
            raise ValueError(f"invalid binary DRAT opcode 0x{op:02x} at byte {pos-1}")
        if op == ord('d'):
            raise ValueError("development RUP checker does not support deletions")
        cur=[]
        while True:
            if pos >= n:
                raise ValueError("unterminated binary DRAT clause")
            first=data[pos]
            if first == 0:
                pos+=1
                break
            code=0
            shift=0
            while True:
                if pos >= n:
                    raise ValueError("unterminated binary DRAT varint")
                byte=data[pos]; pos+=1
                code |= (byte & 0x7f) << shift
                if (byte & 0x80) == 0:
                    break
                shift += 7
                if shift > 63:
                    raise ValueError("binary DRAT varint exceeds 64 bits")
            var=code >> 1
            if var == 0:
                raise ValueError("binary DRAT literal decodes to variable zero")
            cur.append(-var if (code & 1) else var)
        clause=_normalize_clause(cur)
        if clause is not None: clauses.append(clause)
    return clauses


def parse_proof(path: Path):
    data=path.read_bytes()
    if not data:
        return []
    # Binary DRAT is self-identifying: every clause begins with an 'a'/'d'
    # opcode. Textual SatSolv proofs begin with a signed decimal literal or 0.
    if data[0] in (ord('a'), ord('d')):
        return _parse_binary_proof(data)
    return _parse_text_proof(data)


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
