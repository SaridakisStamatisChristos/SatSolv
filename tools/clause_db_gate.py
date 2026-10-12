#!/usr/bin/env python3
"""Predeclared, fail-closed evidence gates for the isolated v2.1 experiment."""
import argparse
import json
from pathlib import Path


def load(path, count, seconds):
    doc = json.loads(path.read_text())
    if doc.get('schema') != 4 or not doc['certification']['passed']:
        raise ValueError(f'{path}: schema/certification failure')
    env = doc['environment']
    if (env['time_limit_seconds'] != seconds or env['memory_gib'] != 4 or
            env['verification_time_limit_seconds'] != (180 if seconds == 15 else 300)):
        raise ValueError(f'{path}: benchmark limits changed')
    if len(doc['rows']) != count or len({r['instance'] for r in doc['rows']}) != count:
        raise ValueError(f'{path}: incomplete/duplicate rows')
    for row in doc['rows']:
        r = row['solvers']['satsolv']
        if r['result'] in ('SAT', 'UNSAT') and not r['verified']:
            raise ValueError(f'{path}: unverified result')
        if r.get('crashed') or (not r['timed_out'] and r['exit_code'] not in (0, 10, 20)):
            raise ValueError(f'{path}: abnormal exit')
        if 'stats' in r and 'core_count' in r['stats']:
            s = r['stats']
            if (s['core_count'] + s['tier2_count'] + s['local_count'] !=
                    s['learnt_clauses'] - s['deleted_learnt_clauses']):
                raise ValueError(f'{path}: tier conservation failed')
            if s['deleted_core'] or s['deleted_tier2'] + s['deleted_local'] != s['deleted_learnt_clauses']:
                raise ValueError(f'{path}: deletion conservation failed')
    return doc


def metrics(doc):
    stats = [r['solvers']['satsolv'].get('stats', {}) for r in doc['rows']]
    seconds = sum(s.get('solve_seconds', r['solvers']['satsolv']['elapsed'])
                  for s, r in zip(stats, doc['rows']))
    total = lambda key: sum(s.get(key, 0) for s in stats)
    return dict(doc['summary']['satsolv'],
                conflicts_per_second=total('conflicts') / max(seconds, 1e-9),
                propagations_per_second=total('propagations') / max(seconds, 1e-9),
                conflicts=total('conflicts'), propagations=total('propagations'),
                restarts=total('restarts'), learnt=total('learnt_clauses'),
                deleted=total('deleted_learnt_clauses'),
                live_endpoint=total('learnt_clauses') - total('deleted_learnt_clauses'),
                proof_bytes=sum(r['solvers']['satsolv']['proof_bytes'] for r in doc['rows']))


def compare(base, candidate):
    inputs = lambda d: [(r['instance'], r['sha256'], r['expected']) for r in d['rows']]
    if inputs(base) != inputs(candidate) or base['manifest_sha256'] != candidate['manifest_sha256']:
        raise ValueError('different corpus/order/hashes')
    errors = []
    b, c = metrics(base), metrics(candidate)
    if c['solved'] < b['solved']:
        errors.append('solved-count regression')
    if c['par2'] > b['par2'] * 1.03:
        errors.append('PAR-2 regression exceeds predeclared 3% materiality gate')
    for br, cr in zip(base['rows'], candidate['rows']):
        bv, cv = br['solvers']['satsolv'], cr['solvers']['satsolv']
        if bv['result'] in ('SAT', 'UNSAT') and cv['result'] != bv['result']:
            errors.append(f'baseline solve lost: {br["instance"]}')
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['screen', 'guard', 'full'])
    ap.add_argument('directory', type=Path)
    a = ap.parse_args()
    count, seconds = {'screen': (10, 15), 'guard': (3, 60), 'full': (30, 60)}[a.mode]
    result = {'mode': a.mode, 'passed': False, 'errors': [], 'metrics': {}}
    try:
        candidate = load(a.directory / 'candidate.json', count, seconds)
        result['metrics']['candidate'] = metrics(candidate)
        if a.mode == 'full':
            c = result['metrics']['candidate']
            if c['solved'] < 8 or c['par2'] >= 90.93:
                result['errors'].append('full acceptance requires >=8/30 and PAR-2 <90.93')
        else:
            base = load(a.directory / 'base.json', count, seconds)
            result['metrics']['base'] = metrics(base)
            result['errors'].extend(compare(base, candidate))
            if a.mode == 'screen':
                old = load(a.directory / 'dbv2.json', count, seconds)
                compare(base, old)  # Validate identical inputs; old policy may regress.
                result['metrics']['dbv2'] = metrics(old)
                if metrics(candidate)['live_endpoint'] >= metrics(old)['live_endpoint']:
                    result['errors'].append('no reduced live database versus rejected v2')
    except (KeyError, ValueError, OSError) as e:
        result['errors'].append(str(e))
    result['passed'] = not result['errors']
    (a.directory / 'gate.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)


if __name__ == '__main__':
    main()
