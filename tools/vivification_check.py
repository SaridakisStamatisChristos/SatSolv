#!/usr/bin/env python3
"""Independent original-CNF truth, SAT witness and DRAT checks for watched vivification."""
import argparse
import itertools
import json
import random
import subprocess
import tempfile
from pathlib import Path
import check_model
import check_rup
import exhaustive_check


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary', type=Path)
    ap.add_argument('--cases', type=int, default=150)
    ap.add_argument('--drat-trim', type=Path)
    ap.add_argument('--eliminate', action='store_true')
    ap.add_argument('--combined', action='store_true')
    a = ap.parse_args()
    rng = random.Random(0x71F1F1)
    fixtures = [
        (3, [(1, 2, 3), (1, -2), (1, -3)]),
        (4, [(1, 2, 3, 4), (1, 2), (-2, 3), (-3, -4)]),
        (4, [(1,), (-1, 2, 3, 4), (-2, 3), (-3, 4), (-4,)]),
        (3, [(1, 2, 3), (-1, 2, 3), (1, -2), (-1, -2), (-3,)]),
        (4, [(1, 2, 3), (1, 2, 3), (-1, 4), (-4, 2), (-2, -3)]),
    ]
    for _ in range(a.cases):
        n = rng.randint(3, 9)
        clauses = []
        for _ in range(rng.randint(n, 7*n)):
            vs = rng.sample(range(1, n+1), rng.randint(1, min(n, 5)))
            clauses.append(tuple(v if rng.randrange(2) else -v for v in vs))
        fixtures.append((n, clauses))
    checks = 0
    strengthened = 0
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        for n, clauses in fixtures:
            cnf, proof, out, stats = [root/x for x in ['case.cnf','case.drat','case.out','case.json']]
            exhaustive_check.write_cnf(cnf, n, clauses)
            expected = exhaustive_check.truth(n, clauses)
            for seed, budget in itertools.product((1, 999), (0, 1, 64, 16000000)):
                flags = ['--experimental-eliminate', '--eliminate-budget', str(budget)] if a.eliminate else ['--experimental-watched-vivify', '--vivify-budget', str(budget)]
                if a.combined: flags += ['--experimental-watched-vivify', '--experimental-eliminate', '--experimental-simplify', '--experimental-dynamic-lbd']
                if seed == 1: flags.append('--no-congruence')
                p = subprocess.run([str(a.binary.resolve()), str(cnf), '--seed', str(seed), '--proof', str(proof), '--stats-json', str(stats), *flags], capture_output=True, text=True, timeout=20)
                assert p.returncode == (10 if expected else 20), (clauses, flags, p.returncode, p.stderr)
                out.write_text(p.stdout)
                if expected:
                    nv, cs = check_model.parse_cnf(cnf)
                    status, model = check_model.parse_model(out)
                    ok, message = check_model.check(nv, cs, model)
                    assert status == 'SATISFIABLE' and ok, message
                else:
                    ok, message = check_rup.verify_proof(list(check_rup.parse_cnf(cnf)), proof)
                    assert ok, (clauses, flags, message)
                    if a.drat_trim:
                        v = subprocess.run([str(a.drat_trim.resolve()), str(cnf), str(proof)], capture_output=True, text=True, timeout=20)
                        assert v.returncode == 0 and any(line.strip() == 's VERIFIED' for line in v.stdout.splitlines()), v.stdout
                s = json.loads(stats.read_text())
                assert s['proof_bytes'] == proof.stat().st_size
                if budget == 0 and not a.combined:
                    assert s['elimination_work'] == 0 if a.eliminate else s['vivify_work'] == s['vivify_removed'] == 0
                strengthened += s['eliminated_variables']+s['vivify_removed'] if a.combined else (s['eliminated_variables'] if a.eliminate else s['vivify_removed'])
                checks += 1
    assert strengthened > 0, 'no strengthening path was exercised'
    print(f'PASS: {checks} '+('elimination' if a.eliminate else 'watched-vivification')+f' original truth/model/RUP checks; {strengthened} reductions; budgets 0/1/64/full')


if __name__ == '__main__':
    main()
