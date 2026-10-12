#!/usr/bin/env python3
"""Independent RUP checker for SatSolv's proof subset.

SatSolv intentionally emits only RUP additions (a valid DRAT subset) and no
deletions.  The development oracle accepts both ordinary text DRAT and the
binary DRAT encoding used by Core v2.  Acceptance still uses external
``drat-trim``; this checker is deliberately independent and small enough to
serve as an exhaustive-test oracle.
"""
from __future__ import annotations

import argparse
from pathlib import Path


def _canonical_clause(lits):
    uniq = set(lits)
    if any(-lit in uniq for lit in uniq):
        return None
    return tuple(sorted(uniq, key=lambda lit: (abs(lit), lit)))


def parse_cnf(path: Path):
    clauses = []
    cur = []
    for raw in path.read_text().splitlines():
        s = raw.strip()
        if not s or s.startswith('c') or s.startswith('p'):
            continue
        for token in s.split():
            lit = int(token)
            if lit == 0:
                clause = _canonical_clause(cur)
                if clause is not None:
                    clauses.append(clause)
                cur = []
            else:
                cur.append(lit)
    if cur:
        raise ValueError("unterminated CNF clause")
    return clauses


def _parse_text_proof(data: bytes):
    clauses = []
    cur = []
    text = data.decode('utf-8')
    for raw in text.splitlines():
        s = raw.strip()
        if not s or s.startswith('c'):
            continue
        if s.startswith('d '):
            raise ValueError("development RUP checker does not support deletions")
        for token in s.split():
            lit = int(token)
            if lit == 0:
                clause = _canonical_clause(cur)
                if clause is not None:
                    clauses.append(clause)
                cur = []
            else:
                cur.append(lit)
    if cur:
        raise ValueError("unterminated proof clause")
    return clauses


def _read_varint(data: bytes, pos: int):
    value = 0
    shift = 0
    while True:
        if pos >= len(data):
            raise ValueError("truncated binary DRAT integer")
        byte = data[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            return value, pos
        shift += 7
        if shift >= 70:
            raise ValueError("binary DRAT integer is too large")


def _parse_binary_proof(data: bytes):
    clauses = []
    pos = 0
    size = len(data)

    while pos < size:
        tag = data[pos]
        pos += 1
        if tag == ord('d'):
            raise ValueError("development RUP checker does not support deletions")
        if tag != ord('a'):
            raise ValueError(f"invalid binary DRAT record tag 0x{tag:02x}")

        cur = []
        while True:
            code, pos = _read_varint(data, pos)
            if code == 0:
                break
            magnitude = code >> 1
            if magnitude == 0:
                raise ValueError("binary DRAT encoded zero as a literal")
            lit = -magnitude if (code & 1) else magnitude
            cur.append(lit)

        clause = _canonical_clause(cur)
        if clause is not None:
            clauses.append(clause)

    return clauses


def parse_proof(path: Path):
    data = path.read_bytes()
    # Every binary-DRAT clause is zero-byte terminated. Text DRAT never contains
    # NUL bytes, which makes this discriminator unambiguous for SatSolv output.
    if b'\x00' in data:
        return _parse_binary_proof(data)
    return _parse_text_proof(data)


def rup(db, clause):
    assign = {}
    for lit in clause:
        v = abs(lit)
        val = lit < 0
        if v in assign and assign[v] != val:
            return True
        assign[v] = val

    changed = True
    while changed:
        changed = False
        for c in db:
            sat = False
            unassigned = []
            for lit in c:
                v = abs(lit)
                if v not in assign:
                    unassigned.append(lit)
                elif (lit > 0) == assign[v]:
                    sat = True
                    break
            if sat:
                continue
            if not unassigned:
                return True
            if len(unassigned) == 1:
                lit = unassigned[0]
                v = abs(lit)
                val = lit > 0
                if v in assign:
                    if assign[v] != val:
                        return True
                else:
                    assign[v] = val
                    changed = True
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('cnf', type=Path)
    parser.add_argument('proof', type=Path)
    args = parser.parse_args()

    db = list(parse_cnf(args.cnf))
    proof = parse_proof(args.proof)
    if not proof:
        print('FAIL: empty proof')
        return 1
    for index, clause in enumerate(proof, 1):
        if not rup(db, clause):
            print(f'FAIL: proof clause {index} is not RUP: {clause}')
            return 1
        db.append(clause)
    if proof[-1] != ():
        print('FAIL: proof does not end with empty clause')
        return 1
    print(f'PASS: {len(proof)} RUP additions accepted, including final empty clause')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
