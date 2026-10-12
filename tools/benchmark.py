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
        return {'command':[str(x) for x in cmd], 'exit_code':p.returncode,'elapsed':time.perf_counter()-t0,
                'stdout':p.stdout,'stderr':p.stderr,'timed_out':False,'crashed':p.returncode<0}
    except subprocess.TimeoutExpired as e:
        stdout=e.stdout.decode(errors='replace') if isinstance(e.stdout,bytes) else (e.stdout or '')
        stderr=e.stderr.decode(errors='replace') if isinstance(e.stderr,bytes) else (e.stderr or '')
        return {'command':[str(x) for x in cmd], 'exit_code':None,'elapsed':time.perf_counter()-t0,
                'stdout':stdout,'stderr':stderr,'timed_out':True,'crashed':False}


def capture_version(binary: Path):
    for args in (['--version'], ['-version']):
        try:
            p=subprocess.run([str(binary.resolve()),*args],capture_output=True,text=True,timeout=5)
        except Exception:
            continue
        text=(p.stdout+'\n'+p.stderr).strip()
        if text:
            return {'command':[str(binary.resolve()),*args], 'exit_code':p.returncode, 'output':text}
    return {'command':[str(binary.resolve()),'--version'], 'exit_code':None, 'output':'UNAVAILABLE'}


def git_commit(root: Path):
    try:
        p=subprocess.run(['git','-C',str(root.resolve()),'rev-parse','HEAD'],capture_output=True,text=True,timeout=5)
        if p.returncode == 0: return p.stdout.strip()
    except Exception:
        pass
    return 'UNAVAILABLE'


def verify_sat(checker, cnf, stdout, work):
    out=work/'solver.out'; out.write_text(stdout)
    p=subprocess.run(['python3',str(checker),str(cnf),str(out)],capture_output=True,text=True)
    return p.returncode==0, {'command':['python3',str(checker),str(cnf),str(out)],'exit_code':p.returncode,'stdout':p.stdout,'stderr':p.stderr}


def verify_unsat(drat_trim, cnf, proof, timeout):
    if not proof.exists():
        return False, {'command':[str(drat_trim),str(cnf),str(proof)],'exit_code':None,'stdout':'','stderr':'missing proof'}
    try:
        p=subprocess.run([str(drat_trim),str(cnf),str(proof)],capture_output=True,text=True,timeout=timeout)
        verdict = any(line.strip() == 's VERIFIED' for line in p.stdout.splitlines())
        return verdict, {'command':[str(drat_trim),str(cnf),str(proof)],'exit_code':p.returncode,'stdout':p.stdout,'stderr':p.stderr,'verified_verdict':verdict}
    except subprocess.TimeoutExpired as e:
        stdout=e.stdout.decode(errors='replace') if isinstance(e.stdout,bytes) else (e.stdout or '')
        stderr=e.stderr.decode(errors='replace') if isinstance(e.stderr,bytes) else (e.stderr or '')
        return False, {'command':[str(drat_trim),str(cnf),str(proof)],'exit_code':None,'stdout':stdout,'stderr':stderr,'timed_out':True}


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
    ap.add_argument('--verification-time-limit',type=float,default=300.0,
                    help='Wall-clock seconds allowed for independent proof verification per UNSAT result')
    ap.add_argument('--memory-gib',type=float,default=4.0)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args()
    manifest=json.loads(a.manifest.read_text()); rows=[]; memory=int(a.memory_gib*(1024**3))
    checker=Path(__file__).resolve().parent/'check_model.py'
    toolchain={'satsolv':capture_version(a.satsolv),'drat_trim':capture_version(a.drat_trim)}
    if a.minisat: toolchain['minisat']=capture_version(a.minisat)
    if a.kissat: toolchain['kissat']=capture_version(a.kissat)
    certification_violations=[]
    with tempfile.TemporaryDirectory() as td:
      work=Path(td)
      for item in manifest['instances']:
        cnf=(a.root/item['path']).resolve()
        row={'instance':item['name'],'source':item.get('url'),'expected':item.get('expected'),'sha256':sha256(cnf),'solvers':{}}
        if item.get('sha256') and row['sha256'] != item['sha256']:
            raise SystemExit(f"hash mismatch for {item['name']}")
        proof=work/(item['name'].replace('/','_')+'.drat')
        stats=work/(item['name'].replace('/','_')+'.json')
        r=run_cmd([str(a.satsolv.resolve()),str(cnf),'--proof',str(proof),'--stats-json',str(stats),'--time-limit',str(a.time_limit)],a.time_limit+2,memory)
        logical='TIMEOUT' if r['timed_out'] else classify(r['stdout'])
        verified=False; verification={'exit_code':None,'stdout':'','stderr':'not run'}
        if logical=='SAT': verified,verification=verify_sat(checker,cnf,r['stdout'],work)
        elif logical=='UNSAT': verified,verification=verify_unsat(a.drat_trim.resolve(),cnf,proof,a.verification_time_limit)
        sat_record=dict(r)
        sat_record.update({'result':logical,'verified':verified,'verification':verification,'proof_bytes':proof.stat().st_size if proof.exists() else 0})
        if stats.exists():
            try: sat_record['stats']=json.loads(stats.read_text())
            except json.JSONDecodeError: sat_record['stats_parse_error']=stats.read_text()
        row['solvers']['satsolv']=sat_record
        expected=item.get('expected')
        if r['crashed']:
            certification_violations.append({'instance':item['name'],'solver':'satsolv','kind':'crash','exit_code':r['exit_code']})
        elif not r['timed_out'] and r['exit_code'] not in (0,10,20):
            certification_violations.append({'instance':item['name'],'solver':'satsolv','kind':'solver_error',
                                             'exit_code':r['exit_code'],'stderr':r['stderr'][-1000:]})
        if logical in ('SAT','UNSAT'):
            if not verified:
                certification_violations.append({'instance':item['name'],'solver':'satsolv','kind':'verification_failure','result':logical})
            if expected in ('SAT','UNSAT') and logical != expected:
                certification_violations.append({'instance':item['name'],'solver':'satsolv','kind':'wrong_result','expected':expected,'result':logical})
        for name,binary in [('minisat',a.minisat),('kissat',a.kissat)]:
            if binary:
                br=run_cmd([str(binary.resolve()),str(cnf)],a.time_limit,memory)
                br['result']='TIMEOUT' if br['timed_out'] else classify(br['stdout']+'\n'+br['stderr'])
                row['solvers'][name]=br
                if br['result'] in ('SAT','UNSAT') and expected in ('SAT','UNSAT') and br['result'] != expected:
                    certification_violations.append({'instance':item['name'],'solver':name,'kind':'baseline_disagrees_with_manifest','expected':expected,'result':br['result']})
        rows.append(row)
    summary={}
    for name in ['satsolv','minisat','kissat']:
        vals=[r['solvers'][name] for r in rows if name in r['solvers']]
        if not vals: continue
        solved=[v for v in vals if v['result'] in ('SAT','UNSAT') and (name!='satsolv' or v.get('verified'))]
        times=[v['elapsed'] for v in solved]
        par2=[v['elapsed'] if (v['result'] in ('SAT','UNSAT') and (name!='satsolv' or v.get('verified'))) else 2*a.time_limit for v in vals]
        summary[name]={'solved':len(solved),'timeouts':sum(v['timed_out'] for v in vals),'crashes':sum(v['crashed'] for v in vals),
                       'solver_errors':sum(1 for v in vals if name=='satsolv' and not v['timed_out'] and not v['crashed'] and v.get('exit_code') not in (0,10,20)),
                       'verification_failures':sum(1 for v in vals if name=='satsolv' and v['result'] in ('SAT','UNSAT') and not v.get('verified')),
                       'median_solved_seconds':statistics.median(times) if times else None,'geomean_solved_seconds':geomean(times),'par2':sum(par2)/len(par2)}
    certification={'passed':not certification_violations,'violations':certification_violations}
    out={'schema':4,'environment':{'platform':platform.platform(),'python':platform.python_version(),'time_limit_seconds':a.time_limit,
                                  'verification_time_limit_seconds':a.verification_time_limit,'memory_gib':a.memory_gib,
                                  'repository_commit':git_commit(Path('.')),'toolchain':toolchain},
         'manifest':str(a.manifest),'manifest_sha256':sha256(a.manifest),'rows':rows,'summary':summary,'certification':certification}
    a.output.parent.mkdir(parents=True,exist_ok=True); a.output.write_text(json.dumps(out,indent=2,sort_keys=True)+'\n')
    print(json.dumps(summary,indent=2,sort_keys=True))
    if certification_violations:
        print(json.dumps({'certification_violations':certification_violations},indent=2,sort_keys=True))
        raise SystemExit(3)
if __name__=='__main__': main()
