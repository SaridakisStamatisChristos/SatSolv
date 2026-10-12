#!/usr/bin/env python3
"""Exercise actual database reductions and externally certify a nontrivial proof."""
import argparse
import json
import subprocess
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary', type=Path)
    ap.add_argument('drat_trim', type=Path)
    ap.add_argument('output', type=Path)
    a = ap.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    cnf, proof, stats = [a.output / f'blocking12.{suffix}' for suffix in ('cnf', 'drat', 'json')]
    with cnf.open('w') as f:
        f.write('p cnf 12 4096\n')
        for mask in range(4096):
            f.write(' '.join(str(-v if mask & (1 << (v - 1)) else v) for v in range(1, 13)) + ' 0\n')
    p = subprocess.run([str(a.binary.resolve()), str(cnf), '--proof', str(proof),
                        '--stats-json', str(stats), '--time-limit', '60'],
                       capture_output=True, text=True, timeout=65)
    (a.output / 'solver.stdout').write_text(p.stdout)
    (a.output / 'solver.stderr').write_text(p.stderr)
    s = json.loads(stats.read_text())
    if p.returncode != 20 or not s['reductions'] or not s['deleted_learnt_clauses']:
        raise SystemExit('UNSAT with actual learnt-clause deletion was not demonstrated')
    v = subprocess.run([str(a.drat_trim.resolve()), str(cnf), str(proof)],
                       capture_output=True, text=True, timeout=180)
    (a.output / 'drat-trim.stdout').write_text(v.stdout)
    (a.output / 'drat-trim.stderr').write_text(v.stderr)
    if 's VERIFIED' not in [line.strip() for line in v.stdout.splitlines()]:
        raise SystemExit('external DRAT certification failed')
    print('PASS: externally certified UNSAT after', s['reductions'], 'reductions and',
          s['deleted_learnt_clauses'], 'deletions')


if __name__ == '__main__':
    main()
