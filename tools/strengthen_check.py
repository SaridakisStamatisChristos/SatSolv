#!/usr/bin/env python3
"""Independent original-CNF truth, model, RUP and external DRAT checks."""
import argparse
import itertools
import json
import hashlib
import random
import subprocess
import tempfile
from pathlib import Path
import core_search_check
import exhaustive_check
import check_rup

SSR = ['--experimental-short-ssr']
GRAPH = ['--experimental-implication-strengthen']
ARMS = [SSR, GRAPH, SSR + GRAPH,
        SSR + GRAPH + ['--experimental-target-phases', '--search-warmup', '2'],
        SSR + ['--experimental-watched-vivify', '--experimental-eliminate'],
        GRAPH + ['--experimental-binary-minimize', '--experimental-rank-lbd'],
        SSR + GRAPH + ['--strengthen-budget', '31'],
        SSR + GRAPH + ['--strengthen-keep-activity'],
        ['--experimental-root-probe', '--probe-start', '0'],
        ['--experimental-root-probe', '--probe-start', '0', '--probe-budget', '31'],
        ['--experimental-root-probe', '--probe-start', '2', '--experimental-target-phases'],
        SSR + GRAPH + ['--experimental-root-probe', '--probe-start', '0', '--experimental-watched-vivify', '--experimental-eliminate']]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary', type=Path)
    ap.add_argument('--cases', type=int, default=100)
    ap.add_argument('--drat-trim', type=Path)
    ap.add_argument('--pressure', action='store_true')
    a = ap.parse_args()
    rng = random.Random(0x51A65)
    fixtures = [(0, []), (0, [()]),
                (3, [(1, 2, 3), (-1, 2)]),
                (4, [(-1, 4), (-4, 2), (1, 2, 3)]),
                (3, [(-1, 2), (-1, -2), (1, 3)]),
                (2, [(1, 2), (-1, 2), (1, -2), (-1, -2)]),
                (4, [(1, 1, 2), (-1, -1, 2), (3, -3), (4,)])]
    fixtures.append((4, [(-1, 2), (-1, 3), (-2, -3, 4)]))
    for _ in range(a.cases):
        n = rng.randint(3, 9)
        cs = [tuple(v if rng.randrange(2) else -v for v in rng.sample(range(1, n+1), rng.randint(2, min(n, 5))))
              for _ in range(rng.randint(2*n, 7*n))]
        fixtures.append((n, cs))
    sat = unsat = 0
    sat_prefix_additions = 0
    binary = a.binary.resolve()
    verifier = a.drat_trim.resolve() if a.drat_trim else None
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        for (n, cs), arm, seed in itertools.product(fixtures, ARMS, (1, 999)):
            flags = [*arm, '--seed', str(seed), '--reduce-interval', '2', '--target-window', '2']
            if seed == 1: flags += ['--no-congruence']
            result = core_search_check.run_case(binary, n, cs, flags, root, verifier)
            if result:
                # Backward-sliced UNSAT checking can ignore unused additions.
                # Independently check every addition even on SAT executions.
                db = list(check_rup.parse_cnf(root/'case.cnf'))
                for deletion, clause in check_rup.parse_records(root/'case.drat'):
                    if deletion:
                        if clause in db: db.remove(clause)
                    else:
                        assert clause and check_rup.rup(db, clause), ('non-RUP SAT-run addition', clause, flags)
                        db.append(clause); sat_prefix_additions += 1
            stats = json.loads((root/'case.json').read_text())
            assert stats['strengthen_work'] <= (31 if '31' in arm else 8000000)
            if '--experimental-short-ssr' not in arm: assert stats['ssr_removed'] == 0
            if '--experimental-implication-strengthen' not in arm:
                assert stats['implication_removed'] == stats['binary_failed_units'] == 0
            if '--experimental-root-probe' not in arm:
                assert stats['probe_rounds'] == stats['probe_attempts'] == stats['probe_units'] == stats['probe_binaries'] == 0
            else:
                assert stats['probe_work'] <= (31 if '--probe-budget' in arm else 1000000)*stats['probe_rounds']
                assert stats['probe_attempts'] <= 256*stats['probe_rounds']
            sat += int(result); unsat += int(not result)
        # A disabled-budget transformation must preserve the entire proof trace.
        hashes = []
        for flags in ([], SSR + GRAPH + ['--strengthen-budget', '0'],
                      ['--experimental-root-probe', '--probe-start', '0', '--probe-budget', '0']):
            p = subprocess.run([str(binary), str(root/'case.cnf'), '--proof', str(root/'zero.drat'), *flags], capture_output=True, timeout=30)
            assert p.returncode in (10, 20)
            hashes.append((p.returncode, p.stdout, (root/'zero.drat').read_bytes()))
        assert hashes[0] == hashes[1] == hashes[2]
        p = subprocess.run([str(binary), str(root/'case.cnf'), '--strengthen-budget', '-1'], capture_output=True)
        assert p.returncode == 2
    print(f'PASS: {sat+unsat} strengthening truth/model/RUP checks ({sat} SAT, {unsat} UNSAT); external DRAT {bool(verifier)}; zero budget byte-identical')
    print(f'PASS: every SAT-run proof addition independently RUP checked ({sat_prefix_additions} additions)')
    if a.pressure:
        if not verifier: raise SystemExit('--pressure requires external DRAT verifier')
        out = Path('benchmarks/results/root-probe-pressure'); out.mkdir(parents=True, exist_ok=True)
        cnf, proof, stats = out/'blocking14-chain.cnf', out/'chain.drat', out/'chain.json'
        clauses = [tuple(-v if bits & (1 << (v-1)) else v for v in range(1, 15)) for bits in range(1 << 14)]
        clauses += [(-15, 16), (-15, 17), (-16, -17, 18)]
        exhaustive_check.write_cnf(cnf, 18, clauses)
        command = [str(binary), str(cnf), '--proof', str(proof), '--stats-json', str(stats),
                   '--experimental-root-probe', '--probe-start', '7000', '--probe-budget', '1000000', '--reduce-interval', '100']
        p = subprocess.run(command, capture_output=True, text=True, timeout=120)
        assert p.returncode == 20, (p.returncode, p.stderr)
        s = json.loads(stats.read_text())
        assert s['deleted_learnt_clauses'] > 4096 and s['descriptor_compactions'] > 0
        assert s['probe_rounds'] > 0 and s['probe_binaries'] > 0
        assert s['probe_work'] <= 1000000*s['probe_rounds'] and s['probe_attempts'] <= 256*s['probe_rounds']
        assert s['proof_bytes'] == proof.stat().st_size and sum(s['birth_lbd_histogram']) == s['learnt_clauses']
        v = subprocess.run([str(verifier), str(cnf), str(proof)], capture_output=True, text=True, timeout=300)
        (out/'chain.verification.log').write_text(v.stdout+v.stderr)
        assert 's VERIFIED' in [line.strip() for line in v.stdout.splitlines()]
        evidence = {'command': command, 'stats': s, 'verified': True,
                    'cnf_sha256': hashlib.sha256(cnf.read_bytes()).hexdigest(),
                    'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                    'proof_sha256': hashlib.sha256(proof.read_bytes()).hexdigest()}
        (out/'evidence.json').write_text(json.dumps(evidence, indent=2)+'\n')
        print('PASS: actual root-probe binary additions after >4096 deletions and real descriptor compaction; external DRAT verified')

if __name__ == '__main__': main()
