#!/usr/bin/env python3
"""Independent original-CNF truth/model/RUP/DRAT checks for search policies."""
import argparse
import itertools
import json
import random
import subprocess
import tempfile
import hashlib
from pathlib import Path
import check_model
import check_rup
import exhaustive_check

ARMS = [[], ['--experimental-dynamic-lbd'],
        ['--experimental-dynamic-lbd', '--experimental-fixed-lbd-tier'],
        ['--experimental-rank-lbd'], ['--experimental-target-phases'],
        ['--experimental-reason-bump'], ['--experimental-recent-use'],
        ['--experimental-target-phases', '--experimental-reason-bump'],
        ['--experimental-rank-lbd', '--experimental-target-phases', '--experimental-reason-bump', '--experimental-recent-use'],
        ['--experimental-rank-lbd', '--experimental-watched-vivify'],
        ['--experimental-rank-lbd', '--experimental-eliminate'],
        ['--experimental-target-phases', '--experimental-reason-bump', '--experimental-watched-vivify', '--experimental-eliminate'],
        ['--experimental-rank-lbd', '--search-warmup', '2'],
        ['--experimental-target-phases', '--experimental-reason-bump', '--search-warmup', '2'],
        ['--experimental-rank-lbd', '--experimental-target-phases', '--experimental-reason-bump', '--experimental-recent-use', '--search-warmup', '2'],
        ['--experimental-rank-lbd', '--experimental-target-phases', '--experimental-reason-bump', '--experimental-recent-use', '--search-warmup', '18446744073709551615'],
        ['--experimental-rank-lbd', '--experimental-target-phases', '--experimental-reason-bump', '--experimental-recent-use', '--search-warmup-seconds', '0.000000001']]

def run_case(binary, n, clauses, flags, root, verifier):
    cnf, proof, output, stats = [root/x for x in ('case.cnf','case.drat','case.out','case.json')]
    exhaustive_check.write_cnf(cnf, n, clauses)
    expected = exhaustive_check.truth(n, clauses)
    p = subprocess.run([str(binary), str(cnf), '--proof', str(proof), '--stats-json', str(stats), *flags], capture_output=True, text=True, timeout=30)
    assert p.returncode == (10 if expected else 20), (n, clauses, flags, p.returncode, p.stderr)
    output.write_text(p.stdout)
    if expected:
        nv, cs = check_model.parse_cnf(cnf)
        status, model = check_model.parse_model(output)
        ok, message = check_model.check(nv, cs, model)
        assert status == 'SATISFIABLE' and ok, message
    else:
        ok, message = check_rup.verify_proof(list(check_rup.parse_cnf(cnf)), proof)
        assert ok, (flags, message)
        if verifier:
            v = subprocess.run([str(verifier), str(cnf), str(proof)], capture_output=True, text=True, timeout=30)
            # This pinned checker can return nonzero on trivial UNSAT; its
            # affirmative, exact verdict is the existing certification contract.
            assert 's VERIFIED' in [line.strip() for line in v.stdout.splitlines()], v.stdout
    s = json.loads(stats.read_text())
    assert s['proof_bytes'] == proof.stat().st_size
    assert sum(s['birth_lbd_histogram']) == s['learnt_clauses']
    assert s['reason_bump_scans'] <= 128 * s['conflicts']
    if '--experimental-rank-lbd' in flags:
        assert s['lbd_tier_crossings'] == 0
        assert s['peak_protected_clauses'] == s['peak_birth_protected_clauses']
    if '--experimental-target-phases' not in flags:
        assert s['target_captures'] == s['target_decisions'] == 0
    return expected

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary', type=Path)
    ap.add_argument('--cases', type=int, default=100)
    ap.add_argument('--drat-trim', type=Path)
    ap.add_argument('--pressure', action='store_true')
    ap.add_argument('--evidence-dir', type=Path, default=Path('benchmarks/results/core-search-pressure'))
    a = ap.parse_args(); binary = a.binary.resolve()
    verifier = a.drat_trim.resolve() if a.drat_trim else None
    rng = random.Random(0x5EA2C)
    fixtures = [(0, []), (0, [()]), (4, [(1,2),(-1,3),(-2,4),(-3,-4)])]
    for _ in range(a.cases):
        n = rng.randint(3,9)
        cs = [tuple(v if rng.randrange(2) else -v for v in rng.sample(range(1,n+1),rng.randint(2,min(n,5)))) for _ in range(rng.randint(2*n,7*n))]
        fixtures.append((n,cs))
    sat = unsat = 0
    baseline_proofs = {}
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        for (n, cs), arm, seed in itertools.product(fixtures, ARMS, (1,999)):
            flags = [*arm, '--seed', str(seed), '--reduce-interval', '2', '--target-window', '2']
            if seed == 1: flags += ['--no-congruence']
            result = run_case(binary,n,cs,flags,root,verifier)
            if not arm:
                baseline_proofs[seed] = hashlib.sha256((root/'case.drat').read_bytes()).hexdigest()
            if '18446744073709551615' in flags:
                assert hashlib.sha256((root/'case.drat').read_bytes()).hexdigest() == baseline_proofs[seed]
                stats = json.loads((root/'case.json').read_text())
                assert stats['search_activations'] == stats['target_captures'] == stats['reason_bumps'] == stats['recent_use_kept'] == 0
            sat += int(result); unsat += int(not result)
        # Check illegal combinations and unsigned-budget parsing fail closed.
        for flags in (['--experimental-rank-lbd','--experimental-dynamic-lbd'],
                      ['--experimental-target-phases','--target-window','0'],
                      ['--reason-bump-budget','-1']):
            p = subprocess.run([str(binary),str(root/'case.cnf'),*flags],capture_output=True,text=True)
            assert p.returncode == 2, (flags,p.returncode,p.stderr)
    print(f'PASS: {sat+unsat} search-policy original-CNF truth/model/RUP checks ({sat} SAT, {unsat} UNSAT); external DRAT {bool(verifier)}')
    if a.pressure:
        if not verifier: raise SystemExit('--pressure requires external drat-trim')
        root = a.evidence_dir; root.mkdir(parents=True, exist_ok=True)
        cnf = root/'blocking14.cnf'
        clauses = [tuple(-v if bits & (1 << (v-1)) else v for v in range(1,15)) for bits in range(1<<14)]
        exhaustive_check.write_cnf(cnf,14,clauses)
        rows = []
        for name, flags in [('rank',['--experimental-rank-lbd']),
                            ('rank-recent',['--experimental-rank-lbd','--experimental-recent-use']),
                            ('target-reason',['--experimental-target-phases','--experimental-reason-bump']),
                            ('delayed-all',['--experimental-rank-lbd','--experimental-target-phases','--experimental-reason-bump','--experimental-recent-use','--search-warmup','1000'])]:
            proof, stats = root/(name+'.drat'),root/(name+'.json')
            command = [str(binary),str(cnf),'--proof',str(proof),'--stats-json',str(stats),'--reduce-interval','100','--target-window','2',*flags]
            p = subprocess.run(command,capture_output=True,text=True,timeout=120)
            assert p.returncode == 20, (name,p.returncode,p.stderr)
            s = json.loads(stats.read_text())
            assert s['deleted_learnt_clauses'] > 4096 and s['descriptor_compactions'] > 0, (name,s)
            assert s['proof_bytes'] == proof.stat().st_size
            assert sum(s['birth_lbd_histogram']) == s['learnt_clauses']
            if name.startswith('rank'):
                assert s['peak_protected_clauses'] == s['peak_birth_protected_clauses'] and s['lbd_tier_crossings'] == 0
            if name == 'delayed-all':
                assert s['search_activations'] == 1 and s['target_resets'] > 0
                assert s['peak_protected_clauses'] == s['peak_birth_protected_clauses'] and s['lbd_tier_crossings'] == 0
            v = subprocess.run([str(verifier),str(cnf),str(proof)],capture_output=True,text=True,timeout=300)
            assert 's VERIFIED' in [line.strip() for line in v.stdout.splitlines()], (name,v.stdout)
            (root/(name+'.verification.log')).write_text(v.stdout+v.stderr)
            rows.append({'arm':name,'command':command,'verified':True,'stats':s,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'cnf_sha256':hashlib.sha256(cnf.read_bytes()).hexdigest()})
            (root/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
        print('PASS: 4 full blocking14 UNSAT proofs after >4096 deletions and real descriptor compaction per arm; delayed activation exercised')

if __name__ == '__main__': main()
