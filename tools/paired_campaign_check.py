#!/usr/bin/env python3
"""Adversarial certification and superiority checks for the paired harness."""
import copy
import paired_campaign as campaign

def main():
    good = {'result': 'SAT', 'kind': 'satsolv', 'crashed': False, 'elapsed': 1.0,
            'verified': True, 'exit_code': 10, 'timed_out': False, 'stats': {'proof_bytes': 0}, 'proof_bytes': 0,
            'within_solver_budget': True}
    assert campaign.validate_record(good, 'SAT') is None
    for field, bad in [('verified', False), ('exit_code', 0), ('crashed', True),
                       ('elapsed', float('nan')), ('elapsed', -1.0), ('stats', None)]:
        record = dict(good); record[field] = bad
        assert campaign.validate_record(record, 'SAT') is not None, (field, bad)
    assert campaign.validate_record(good, 'UNSAT') is not None
    assert campaign.validate_record(dict(good, proof_bytes=1), 'SAT') is not None
    unknown = dict(good, result='UNKNOWN', verified=False, exit_code=0)
    assert campaign.validate_record(unknown, 'SAT') is None
    assert campaign.validate_record(dict(unknown, exit_code=3), 'SAT') is not None
    assert campaign.validate_record(dict(unknown, kind='kissat'), 'SAT') is not None
    timeout = dict(unknown, result='TIMEOUT', timed_out=True, exit_code=None)
    assert campaign.validate_record(timeout, 'SAT') is None
    rows = [{'instance': 'one', 'solvers': {'control': copy.copy(good), 'candidate': copy.copy(good), 'kissat': copy.copy(good)}}]
    verdict = campaign.summarize(rows, ['control', 'candidate', 'kissat'], 60, 1)
    assert verdict['paired_numerical_gate'] and not verdict['strict_kissat_numerical_superiority']
    rows[0]['solvers']['candidate'] = unknown
    verdict = campaign.summarize(rows, ['control', 'candidate', 'kissat'], 60, 1)
    assert not verdict['paired_numerical_gate'] and verdict['lost_control_solves'] == ['one']
    rows[0]['solvers']['candidate'] = good; rows[0]['solvers']['kissat'] = timeout
    verdict = campaign.summarize(rows, ['control', 'candidate', 'kissat'], 60, 1)
    assert verdict['strict_kissat_numerical_superiority']
    assert not verdict['statistical_superiority_established'] and not verdict['general_sota_established']
    rows[0]['solvers']['candidate'] = dict(good, within_solver_budget=False, elapsed=61)
    verdict = campaign.summarize(rows, ['control', 'candidate', 'kissat'], 60, 1)
    assert verdict['summary']['candidate']['solved'] == 0 and verdict['summary']['candidate']['par2'] == 120
    assert not verdict['paired_numerical_gate'] and not verdict['strict_kissat_numerical_superiority']
    rows[0]['solvers']['candidate']['within_solver_budget'] = True
    assert campaign.summarize(rows, ['control', 'candidate', 'kissat'], 60, 1)['summary']['candidate']['solved'] == 0
    print('PASS: certification rejects invalid witnesses, exits, crashes, timing and missing stats; lost solves fail; ties never claim Kissat superiority')

if __name__ == '__main__': main()
