#!/usr/bin/env python3
"""Keep certification, original adoption target and superiority separate."""
import argparse
import json
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('report', type=Path)
    a = ap.parse_args()
    d = json.loads(a.report.read_text())
    s = d['summary']['satsolv']
    adopted = bool(d['certification']['passed'] and len(d['rows']) == 30 and s['solved'] >= 8 and s['par2'] < 90.93)
    comparisons = {}
    for name in ('minisat', 'kissat'):
        b = d['summary'].get(name)
        if b: comparisons[name] = bool(s['solved'] >= b['solved'] and s['par2'] < b['par2'])
    print(json.dumps({'certification': d['certification'], 'original_adoption_target_passed': adopted,
                      'same_run_comparative_superiority': comparisons,
                      'frozen_slice_superiority': adopted and len(comparisons) == 2 and all(comparisons.values())}, indent=2))
    if not adopted: raise SystemExit('Original Core-v2 adoption target unmet; do not merge')


if __name__ == '__main__': main()
