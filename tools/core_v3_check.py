#!/usr/bin/env python3
"""Proof/model differential checks across feature ablations and seeds."""
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
    ap.add_argument('--cases', type=int, default=200)
    ap.add_argument('--drat-trim', type=Path)
    a = ap.parse_args()
    binary = a.binary.resolve()
    configs = [[], ['--no-simplify'], ['--no-binary-minimize'],
               ['--no-simplify', '--no-binary-minimize', '--no-clause-decay']]
    rng = random.Random(0xCAFE751)
    count = 0
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        formulas = [
            (4, [(1, 2), (-1, 3), (-2, 4), (-3, -4)]),
            (3, [(1, 2, 3), (1, 2), (-1, 3)]),
            (3, [(1, 2), (-1, 2), (-2, 3), (-3,)]),
            (4, [(1, -2), (-1, 2), (2, 3), (-3, 4)]),
        ]
        for _ in range(a.cases):
            n = rng.randint(3, 9)
            clauses = []
            for _ in range(rng.randint(n, 6*n)):
                vs = rng.sample(range(1, n+1), rng.randint(2, min(n, 4)))
                clauses.append(tuple(v if rng.randrange(2) else -v for v in vs))
            formulas.append((n, clauses))
        for n, clauses in formulas:
            cnf = root/'case.cnf'; proof = root/'case.drat'; out = root/'case.out'
            exhaustive_check.write_cnf(cnf, n, clauses)
            expected = exhaustive_check.truth(n, clauses)
            for config, seed in itertools.product(configs, (1, 999)):
                p = subprocess.run([str(binary), str(cnf), '--proof', str(proof), '--seed', str(seed), *config], capture_output=True, text=True, timeout=10)
                out.write_text(p.stdout)
                if p.returncode != (10 if expected else 20):
                    raise AssertionError((n, clauses, config, seed, p.returncode, p.stderr))
                if expected:
                    nv, cs = check_model.parse_cnf(cnf)
                    status, model = check_model.parse_model(out)
                    ok, message = check_model.check(nv, cs, model)
                    if status != 'SATISFIABLE' or not ok: raise AssertionError(message)
                else:
                    ok, message = check_rup.verify_proof(list(check_rup.parse_cnf(cnf)), proof)
                    if not ok: raise AssertionError((config, seed, message))
                count += 1
        # Contradict every 14-bit assignment: real reductions and ID remapping.
        cnf = root/'blocking14.cnf'; proof = root/'blocking14.drat'; stats = root/'stats.json'
        clauses = [tuple(-v if bits & (1 << (v-1)) else v for v in range(1, 15)) for bits in range(1 << 14)]
        exhaustive_check.write_cnf(cnf, 14, clauses)
        p = subprocess.run([str(binary), str(cnf), '--no-simplify', '--no-binary-minimize', '--no-clause-decay', '--reduce-interval', '100', '--proof', str(proof), '--stats-json', str(stats), '--telemetry'], capture_output=True, text=True, timeout=60)
        if p.returncode != 20: raise AssertionError(p.stderr)
        s = json.loads(stats.read_text())
        assert s['deleted_learnt_clauses'] > 4096 and s['descriptor_compactions'] > 0
        assert s['peak_heap_entries'] <= 14
        assert s['proof_bytes'] == proof.stat().st_size
        records = check_rup.parse_records(proof)
        assert any(deletion for deletion, _ in records)
        assert records[-1] == (False, ())
        if a.drat_trim:
            p = subprocess.run([str(a.drat_trim.resolve()), str(cnf), str(proof)], capture_output=True, text=True, timeout=120)
            if not any(line.strip() == 's VERIFIED' for line in p.stdout.splitlines()):
                raise AssertionError((p.returncode, p.stdout, p.stderr))
            print('PASS: external DRAT after real deletions and descriptor compaction')
        else:
            print('External DRAT not run locally; CI must verify blocking14 proof')
        # A deletion must actually remove justification in the independent oracle.
        cnf.write_text('p cnf 1 2\n1 0\n-1 0\n')
        proof.write_text('d 1 0\n0\n')
        assert not check_rup.verify_proof(list(check_rup.parse_cnf(cnf)), proof)[0]
    print(f'PASS: {count} model/RUP ablation-seed checks; compaction, proof-byte and deletion-oracle invariants')


if __name__ == '__main__':
    main()
