#!/usr/bin/env python3
"""Independent original-CNF truth/model/RUP checks for structural rewriting."""
import argparse
import itertools
import random
import subprocess
import tempfile
from pathlib import Path
import check_model
import check_rup
import exhaustive_check


def gate(output, inputs):
    return [(-output, x) for x in inputs] + [tuple([output, *[-x for x in inputs]])]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary', type=Path)
    ap.add_argument('--cases', type=int, default=100)
    ap.add_argument('--drat-trim', type=Path)
    a = ap.parse_args()
    rng = random.Random(814141)
    fixtures = [
        (2, [(1, 2), (-1, 2), (1, -2), (-1, -2)]),
        # Complemented duplicate gates create a binary signed cycle; a proved
        # unit is needed before the final empty clause is RUP.
        (4, gate(3, [1, 2]) + gate(-3, [1, 2]) + [(1,), (2,)]),
        (4, gate(3, [1, 2]) + gate(4, [1, 2]) + [(3,), (-4,)]),
        (4, gate(-3, [1, -2]) + gate(4, [1, -2]) + [(-3,), (-4,)]),
        # Incomplete encodings must not be mistaken for complete AND gates.
        (4, [(3, -1, -2), (-3, 1), (4, -1, -2), (-4, 2), (3,), (-4,)]),
        (3, [(1, -2), (-1, 2), (2, 3), (-2, -3), (1,)]),
    ]
    for _ in range(a.cases):
        clauses = []
        for v in range(4, 9):
            output = v if rng.randrange(2) else -v
            inputs = [x if rng.randrange(2) else -x for x in rng.sample(range(1, v), 2)]
            clauses += gate(output, inputs)
        # Deliberate complete duplicate with either output polarity.
        polarity = 1 if rng.randrange(2) else -1
        clauses += gate(9, [1, 2]) + gate(polarity*10, [1, 2])
        for _ in range(rng.randrange(6)):
            clauses.append(tuple(x if rng.randrange(2) else -x for x in rng.sample(range(1, 11), rng.choice([1, 2, 3]))))
        fixtures.append((10, clauses))
    checked = 0
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        for n, clauses in fixtures:
            cnf=root/'case.cnf';proof=root/'case.drat';out=root/'case.out'
            exhaustive_check.write_cnf(cnf,n,clauses)
            expected=exhaustive_check.truth(n,clauses)
            for config,seed in itertools.product([[], ['--no-congruence'], ['--experimental-simplify']], (1,999)):
                p=subprocess.run([str(a.binary.resolve()),str(cnf),'--proof',str(proof),'--seed',str(seed),*config],capture_output=True,text=True,timeout=10)
                assert p.returncode==(10 if expected else 20),(clauses,config,seed,p.stdout,p.stderr)
                if expected:
                    out.write_text(p.stdout)
                    nv,cs=check_model.parse_cnf(cnf);status,model=check_model.parse_model(out)
                    ok,message=check_model.check(nv,cs,model)
                    assert status=='SATISFIABLE' and ok,(config,seed,message)
                else:
                    ok,message=check_rup.verify_proof(list(check_rup.parse_cnf(cnf)),proof)
                    assert ok,(clauses,config,seed,message)
                    if a.drat_trim:
                        v=subprocess.run([str(a.drat_trim.resolve()),str(cnf),str(proof)],capture_output=True,text=True,timeout=30)
                        assert any(l.strip()=='s VERIFIED' for l in v.stdout.splitlines()),v.stdout
                checked += 1
    print(f'PASS: {checked} original-CNF structural model/proof checks, complete/incomplete gates, signs and cycle regression')


if __name__=='__main__': main()
