#!/usr/bin/env python3
from __future__ import annotations
import argparse, subprocess, tempfile
from pathlib import Path

CASES = {
    "missing_header": "1 0\n",
    "duplicate_header": "p cnf 1 0\np cnf 1 0\n",
    "out_of_range": "p cnf 1 1\n2 0\n",
    "unterminated": "p cnf 1 1\n1\n",
    "too_many_clauses": "p cnf 1 0\n1 0\n",
    "too_few_clauses": "p cnf 1 2\n1 0\n",
    "bad_token": "p cnf 1 1\nxyz 0\n",
    "bad_header": "p foo 1 1\n1 0\n",
    "negative_count": "p cnf -1 0\n",
}


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('binary',type=Path); a=ap.parse_args(); binary=a.binary.resolve()
    with tempfile.TemporaryDirectory() as d:
        d=Path(d)
        for name,text in CASES.items():
            p=d/f'{name}.cnf'; p.write_text(text)
            r=subprocess.run([str(binary),str(p)],capture_output=True,text=True,timeout=2)
            if r.returncode != 2:
                raise AssertionError(f'{name}: expected rc=2, got {r.returncode}: {r.stdout} {r.stderr}')
            if 'SATISFIABLE' in r.stdout or 'UNSATISFIABLE' in r.stdout:
                raise AssertionError(f'{name}: malformed input produced logical answer')
    print(f'PASS: {len(CASES)} malformed-input cases rejected explicitly')
if __name__=='__main__': main()
