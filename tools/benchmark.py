#!/usr/bin/env python3
"""Resource-limited certified benchmark harness for SatSolv and baseline SAT solvers."""
from __future__ import annotations
import argparse, hashlib, json, math, os, platform, resource, statistics, subprocess, tempfile, time
from pathlib import Path


def sha256(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1<<20),b''): h.update(chunk)
    return h.hexdigest()


def classify(text: str):
    u=text.upper()
    if 'UNSATISFIABLE' in u: return 'UNSAT'
    if 'SATISFIABLE' in u: return 'SAT'
    if 'UNKNOWN' in u: return 'UNKNOWN'
    return 'UNKNOWN'


def limit_memory(bytes_limit: int):
    def apply():
        if bytes_limit > 0:
            resource.setrlimit(resource.RLIMIT_AS,(bytes_limit,bytes_limit))
    return apply


def run_cmd(cmd, timeout, memory_bytes, cwd=None):
    t0=time.perf_counter()
    try:
        p=subprocess.run(cmd,cwd=cwd,capture_output=True,text=True,timeout=timeout,
                         preexec_fn=limit_memory(memory_bytes) if os.name=='posix' else None)
        return {'exit_code':p.returncode,'elapsed':time.perf_counter()-t0,'stdout':p.stdout,'stderr':p.stderr,'timed_out':False,'crashed':p.returncode<0}
    except subprocess.TimeoutExpired as e:
        return {'exit_code':None,'elapsed':time.perf_counter()-t0,'stdout':e.stdout or '', 'stderr':e.stderr or '', 'timed_out':True,'crashed':False}


def verify_sat(checker, cnf, stdout, work):
    out=work/'solver.out'; out.write_text(stdout)
    p=subprocess.run(['python3',str(checker),str(cnf),str(out)],capture_output=True,text=True)
    return p.returncode==0, p.stdout+p.stderr


def verify_unsat(drat_trim, cnf, proof, timeout):
    if not proof.exists(): return False,'missing proof'
    try:
        p=subprocess.run([str(drat_trim),str(cnf),str(proof)],capture_output=True,text=True,timeout=timeout)
        return p.returncode==0, p.stdout+p.stderr
    except subprocess.TimeoutExpired:
        return False,'drat-trim timeout'


def geomean(xs):
    xs=[max(x,1e-9) for x in xs]
    return math.exp(sum(math.log(x) for x in xs)/len(xs)) if xs else None


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--manifest',type=Path,required=True)
    ap.add_argument('--root',type=Path,default=Path('.'))
    ap.add_argument('--satsolv',type=Path,required=True)
    ap.add_argument('--minisat',type=Path)
    ap.add_argument('--kissat',type=Path)
    ap.add_argument('--drat-trim',type=Path,required=True)
    ap.add_argument('--time-limit',type=float,default=60.0)
    ap.add_argument('--memory-gib',type=float,default=4.0)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args()
    manifest=json.loads(a.manifest.read_text()); rows=[]; memory=int(a.memory_gib*(1024**3))
    checker=Path(__file__).resolve().parent/'check_model.py'
    with tempfile.TemporaryDirectory() as td:
      work=Path(td)
      for item in manifest['instances']:
        cnf=(a.root/item['path']).resolve()
        row={'instance':item['name'],'expected':item.get('expected'),'sha256':sha256(cnf),'solvers':{}}
        if item.get('sha256') and row['sha256'] != item['sha256']:
            raise SystemExit(f"hash mismatch for {item['name']}")
        proof=work/(item['name'].replace('/','_')+'.drat')
        stats=work/(item['name'].replace('/','_')+'.json')
        r=run_cmd([str(a.satsolv.resolve()),str(cnf),'--proof',str(proof),'--stats-json',str(stats),'--time-limit',str(a.time_limit)],a.time_limit+2,memory)
        logical='TIMEOUT' if r['timed_out'] else classify(r['stdout'])
        verified=False; verify_log=''
        if logical=='SAT': verified,verify_log=verify_sat(checker,cnf,r['stdout'],work)
        elif logical=='UNSAT': verified,verify_log=verify_unsat(a.drat_trim.resolve(),cnf,proof,a.time_limit)
        row['solvers']['satsolv']={k:r[k] for k in ('exit_code','elapsed','timed_out','crashed')}|{'result':logical,'verified':verified,'verification_log':verify_log[-4000:]}
        for name,binary in [('minisat',a.minisat),('kissat',a.kissat)]:
            if binary:
                br=run_cmd([str(binary.resolve()),str(cnf)],a.time_limit,memory)
                row['solvers'][name]={k:br[k] for k in ('exit_code','elapsed','timed_out','crashed')}|{'result':'TIMEOUT' if br['timed_out'] else classify(br['stdout']+'\n'+br['stderr'])}
        rows.append(row)
    summary={}
    for name in ['satsolv','minisat','kissat']:
        vals=[r['solvers'][name] for r in rows if name in r['solvers']]
        if not vals: continue
        solved=[v for v in vals if v['result'] in ('SAT','UNSAT') and (name!='satsolv' or v.get('verified'))]
        times=[v['elapsed'] for v in solved]
        par2=[v['elapsed'] if (v['result'] in ('SAT','UNSAT') and (name!='satsolv' or v.get('verified'))) else 2*a.time_limit for v in vals]
        summary[name]={'solved':len(solved),'timeouts':sum(v['timed_out'] for v in vals),'crashes':sum(v['crashed'] for v in vals),'median_solved_seconds':statistics.median(times) if times else None,'geomean_solved_seconds':geomean(times),'par2':sum(par2)/len(par2)}
    out={'schema':1,'environment':{'platform':platform.platform(),'python':platform.python_version(),'time_limit_seconds':a.time_limit,'memory_gib':a.memory_gib},'manifest':str(a.manifest),'rows':rows,'summary':summary}
    a.output.parent.mkdir(parents=True,exist_ok=True); a.output.write_text(json.dumps(out,indent=2,sort_keys=True)+'\n')
    print(json.dumps(summary,indent=2,sort_keys=True))
if __name__=='__main__': main()
