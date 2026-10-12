#!/usr/bin/env python3
"""Generate a human-readable benchmark report from raw benchmark JSON evidence."""
from __future__ import annotations
import argparse, json
from pathlib import Path


def cell(v, certified=False):
    if not v: return '—'
    result=v.get('result','UNKNOWN')
    elapsed=v.get('elapsed')
    suffix=f" {elapsed:.3f}s" if isinstance(elapsed,(int,float)) else ''
    if certified and result in ('SAT','UNSAT'):
        result += ' ✓' if v.get('verified') else ' ✗'
    return result+suffix


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('input',type=Path); ap.add_argument('output',type=Path); a=ap.parse_args()
    data=json.loads(a.input.read_text()); lines=[]
    certification=data.get('certification')
    if certification is None:
        certification_label='UNAVAILABLE (legacy evidence schema)'
    else:
        certification_label='PASS' if certification.get('passed') is True else 'FAIL'

    env=data.get('environment',{})
    lines += [
        '# SatSolv benchmark report','',
        f"**Certification: {certification_label}**",'',
        f"Raw evidence: `{a.input}`",
        f"Repository commit: `{env.get('repository_commit','UNAVAILABLE')}`",
        f"Manifest SHA-256: `{data.get('manifest_sha256','UNAVAILABLE')}`",
        f"Solver wall-clock limit: `{env.get('time_limit_seconds','UNAVAILABLE')} s`",
        f"Proof-verification wall-clock limit: `{env.get('verification_time_limit_seconds','UNAVAILABLE')} s`",
        f"Memory limit: `{env.get('memory_gib','UNAVAILABLE')} GiB`",''
    ]

    if certification is not None and not certification.get('passed'):
        violations=certification.get('violations',[])
        lines += ['## Certification violations','']
        if violations:
            lines += ['| Instance | Solver | Kind | Expected | Result |','|---|---|---|---|---|']
            for v in violations:
                lines.append(
                    f"| {v.get('instance','—')} | {v.get('solver','—')} | {v.get('kind','—')} | "
                    f"{v.get('expected','—')} | {v.get('result','—')} |"
                )
        else:
            lines.append('Certification failed without a structured violation record.')
        lines.append('')

    lines += ['| Instance | Expected | SatSolv | SatSolv Verified | MiniSat | Kissat |','|---|---|---:|---|---:|---:|']
    for row in data.get('rows',[]):
        ss=row.get('solvers',{}).get('satsolv'); ms=row.get('solvers',{}).get('minisat'); ks=row.get('solvers',{}).get('kissat')
        verified='YES' if ss and ss.get('verified') else ('NO' if ss and ss.get('result') in ('SAT','UNSAT') else 'N/A')
        lines.append(f"| {row.get('instance','')} | {row.get('expected') or '—'} | {cell(ss, True)} | {verified} | {cell(ms)} | {cell(ks)} |")
    lines += ['','## Aggregate','', '| Solver | Solved | Timeouts | Crashes | Verification failures | Median solved (s) | Geomean solved (s) | PAR-2 |',
              '|---|---:|---:|---:|---:|---:|---:|---:|']
    for name,s in data.get('summary',{}).items():
        fmt=lambda x: '—' if x is None else (f'{x:.6f}' if isinstance(x,float) else str(x))
        lines.append(f"| {name} | {s.get('solved',0)} | {s.get('timeouts',0)} | {s.get('crashes',0)} | {s.get('verification_failures',0)} | {fmt(s.get('median_solved_seconds'))} | {fmt(s.get('geomean_solved_seconds'))} | {fmt(s.get('par2'))} |")
    a.output.parent.mkdir(parents=True,exist_ok=True); a.output.write_text('\n'.join(lines)+'\n')
    print(a.output)
if __name__=='__main__': main()
