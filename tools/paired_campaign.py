#!/usr/bin/env python3
"""Blocked, certified comparison of pinned executables on a fixed manifest.

Writes every completed observation before checking the final numerical gates.
No expected-truth information is passed to a solver or used for arm routing.
"""
import argparse
import json
import math
import platform
import tempfile
from pathlib import Path
import benchmark


def validate_record(record, expected):
    result = record['result']
    kind = record['kind']
    if record['crashed'] or not math.isfinite(record['elapsed']) or record['elapsed'] < 0:
        return 'crash or invalid timing'
    if result in ('SAT', 'UNSAT'):
        if not record['verified']: return 'unverified definitive result'
        if record['exit_code'] != (10 if result == 'SAT' else 20): return 'invalid definitive exit code'
        if result != expected: return 'truth disagreement'
    elif record['timed_out']:
        if result != 'TIMEOUT': return 'invalid timeout classification'
    elif kind == 'satsolv' and result == 'UNKNOWN' and record['exit_code'] == 0:
        pass
    else:
        return 'solver error or unsupported nondefinitive exit'
    if kind == 'satsolv' and not record['timed_out'] and not isinstance(record.get('stats'), dict):
        return 'missing or invalid native statistics'
    if kind == 'satsolv' and not record['timed_out'] and record['stats'].get('proof_bytes') != record['proof_bytes']:
        return 'native proof-byte accounting mismatch'
    return None


def summarize(rows, names, time_limit, minimum_solves):
    def eligible(r): return r['verified'] and not r['timed_out'] and r['elapsed'] <= time_limit
    summaries = {}
    for name in names:
        observations = [row['solvers'][name] for row in rows]
        summaries[name] = {
            'solved': sum(eligible(r) for r in observations),
            'par2': sum(r['elapsed'] if eligible(r) else 2*time_limit for r in observations)/len(rows)}
    lost = [r['instance'] for r in rows if eligible(r['solvers']['control']) and not eligible(r['solvers']['candidate'])]
    gained = [r['instance'] for r in rows if not eligible(r['solvers']['control']) and eligible(r['solvers']['candidate'])]
    c, b = summaries['candidate'], summaries['control']
    numerical_gate = not lost and c['solved'] >= minimum_solves and c['solved'] >= b['solved'] and c['par2'] <= b['par2']
    k = summaries.get('kissat')
    return {'summary': summaries, 'lost_control_solves': lost, 'gained_control_solves': gained,
            'paired_numerical_gate': numerical_gate,
            'more_solves_than_kissat': None if k is None else c['solved'] > k['solved'],
            'strict_kissat_numerical_superiority': None if k is None else c['solved'] > k['solved'] and c['par2'] < k['par2'],
            'statistical_superiority_established': False,
            'general_sota_established': False}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--manifest', type=Path, required=True)
    ap.add_argument('--config', type=Path, required=True)
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--drat-trim', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--time-limit', type=float, default=60)
    ap.add_argument('--verification-time-limit', type=float, default=300)
    ap.add_argument('--memory-gib', type=float, default=4)
    ap.add_argument('--order-offset', type=int, default=0)
    ap.add_argument('--minimum-solves', type=int, default=10)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--require-paired-gate', action='store_true')
    a = ap.parse_args()
    if any(not math.isfinite(x) or x <= 0 for x in (a.time_limit, a.verification_time_limit, a.memory_gib)):
        raise SystemExit('budgets must be finite and positive')
    if a.seed < 0 or a.seed > 4294967295: raise SystemExit('invalid common solver seed')
    config = json.loads(a.config.read_text())
    manifest = json.loads(a.manifest.read_text())
    if benchmark.sha256(a.manifest) != config['manifest_sha256']: raise SystemExit('manifest pin mismatch')
    items = manifest['instances']
    if len(items) != config['instance_count'] or len({i['name'] for i in items}) != len(items) or not items:
        raise SystemExit('incomplete or duplicate input set')
    arms = config['arms']
    names = [arm['name'] for arm in arms]
    if len(set(names)) != len(names) or not {'candidate', 'control'} <= set(names): raise SystemExit('invalid arm set')
    for arm in arms:
        if arm['kind'] not in ('satsolv', 'kissat') or not arm.get('source_commit'):
            raise SystemExit('missing engine kind/source pin')
        if any(f.split('=')[0] in ('--proof', '--stats-json', '--time-limit', '--seed') for f in arm['flags']):
            raise SystemExit('arm overrides shared invocation contract')
        arm['binary'] = str(Path(arm['binary']).resolve())
        arm['binary_sha256'] = benchmark.sha256(Path(arm['binary']))
    verifier = a.drat_trim.resolve()
    checker = Path(__file__).resolve().parent/'check_model.py'
    cpu = platform.processor() or 'UNAVAILABLE'
    cpuinfo = Path('/proc/cpuinfo')
    if cpuinfo.exists():
        cpu = next((line.split(':', 1)[1].strip() for line in cpuinfo.read_text().splitlines() if line.startswith('model name')), cpu)
    document = {'pins': {'config': config, 'manifest_sha256': benchmark.sha256(a.manifest),
                         'drat_trim_sha256': benchmark.sha256(verifier), 'harness_commit': benchmark.git_commit(Path('.'))},
                'environment': {'platform': platform.platform(), 'cpu': cpu, 'python': platform.python_version(),
                                'seed': a.seed, 'order_offset': a.order_offset,
                                'solver_seconds': a.time_limit, 'proof_seconds': a.verification_time_limit,
                                'memory_gib': a.memory_gib},
                'rows': [], 'certification': {'passed': True, 'violations': []}}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    def save(): a.output.write_text(json.dumps(document, indent=2)+'\n')
    save()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        for index, item in enumerate(items):
            cnf = (a.root/item['path']).resolve()
            if item['expected'] not in ('SAT', 'UNSAT') or benchmark.sha256(cnf) != item['sha256']:
                raise SystemExit('invalid truth/hash contract for '+item['name'])
            row = {'instance': item['name'], 'sha256': item['sha256'], 'expected': item['expected'], 'solvers': {}}
            document['rows'].append(row)
            rotation = (index + a.order_offset) % len(arms)
            order = arms[rotation:] + arms[:rotation]
            if ((index + a.order_offset)//len(arms)) % 2: order = list(reversed(order))
            row['execution_order'] = [arm['name'] for arm in order]
            for arm in order:
                proof, stats = work/'case.drat', work/'case.json'
                proof.unlink(missing_ok=True); stats.unlink(missing_ok=True)
                if arm['kind'] == 'satsolv':
                    command = [arm['binary'], str(cnf), '--proof', str(proof), '--stats-json', str(stats),
                               '--time-limit', str(a.time_limit), '--seed', str(a.seed), *arm['flags']]
                    timeout = a.time_limit
                else:
                    command = [arm['binary'], '--seed='+str(a.seed), *arm['flags'], str(cnf), str(proof)]
                    timeout = a.time_limit
                record = benchmark.run_cmd(command, timeout, int(a.memory_gib*1024**3))
                result = 'TIMEOUT' if record['timed_out'] else benchmark.classify(record['stdout'])
                verified = False
                verification = {'exit_code': None, 'stdout': '', 'stderr': 'not run'}
                if result == 'SAT': verified, verification = benchmark.verify_sat(checker, cnf, record['stdout'], work)
                elif result == 'UNSAT': verified, verification = benchmark.verify_unsat(verifier, cnf, proof, a.verification_time_limit)
                record.update({'kind': arm['kind'], 'result': result, 'verified': verified,
                               'within_solver_budget': not record['timed_out'] and record['elapsed'] <= a.time_limit,
                               'verification': verification, 'proof_bytes': proof.stat().st_size if proof.exists() else 0})
                if stats.exists():
                    try: record['stats'] = json.loads(stats.read_text())
                    except json.JSONDecodeError: record['stats_parse_error'] = True
                row['solvers'][arm['name']] = record
                failure = validate_record(record, item['expected'])
                if failure:
                    document['certification']['passed'] = False
                    document['certification']['violations'].append({'instance': item['name'], 'arm': arm['name'], 'error': failure})
                save()
                print(item['name'], arm['name'], result, round(record['elapsed'], 3), 'verified', verified, 'within_budget', record['within_solver_budget'], flush=True)
                if failure: raise SystemExit('certification failed: '+failure)
    document.update(summarize(document['rows'], names, a.time_limit, a.minimum_solves))
    save()
    print(json.dumps({k: document[k] for k in ('summary', 'lost_control_solves', 'gained_control_solves', 'paired_numerical_gate', 'strict_kissat_numerical_superiority')}, indent=2))
    if a.require_paired_gate and not document['paired_numerical_gate']:
        raise SystemExit('prespecified paired gate failed; preserve current defaults')

if __name__ == '__main__': main()
