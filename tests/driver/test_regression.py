# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute fresh isolated acceptance against optional add-on root. Old P5 must fail."""
from pathlib import Path
import argparse,subprocess,sys,json,os
E=Path(__file__).resolve().parent;R=E.parents[1]
p=argparse.ArgumentParser();p.add_argument('--target-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--budget',default='8');p.add_argument('--extended',action='store_true')
p.add_argument('--nvda-runtime',type=Path,default=os.environ.get('NVDA_RUNTIME'))
p.add_argument('--nvda-source',type=Path,default=os.environ.get('NVDA_SOURCE'),help='Optional external NVDA source for provenance, never copied')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
if a.nvda_runtime:os.environ['NVDA_RUNTIME']=str(a.nvda_runtime.resolve())
if not os.environ.get('NVDA_RUNTIME'):p.error('--nvda-runtime or NVDA_RUNTIME is required')
if a.nvda_source:
 assert (a.nvda_source/'copying.txt').is_file() and (a.nvda_source/'source/speech/manager.py').is_file(),'invalid external NVDA source'
 import hashlib
 provenance={str(f.relative_to(a.nvda_source)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [a.nvda_source/'copying.txt',a.nvda_source/'source/speech/manager.py']}
 (a.output/'external-source-provenance.json').write_text(json.dumps(provenance,indent=2))
cases=['short','10s','30s','60s','1kb','10kb','sayall','queued','silent','cancel','pause']
if a.extended:cases+=['100kb']
rows=[];errors=[]
for case in cases:
 cmd=[sys.executable,'-B',str(E/'exercise.py'),'--target-root',str(a.target_root),'--output',str(a.output),'--case',case,'--budget',('30' if case=='100kb' else a.budget)]
 proc=subprocess.run(cmd,capture_output=True,text=True,timeout=35,cwd=R)
 (a.output/(case+'.log')).write_text(proc.stdout+'\nSTDERR\n'+proc.stderr)
 f=a.output/('p5-caller-'+case+'.json')
 if not f.exists():errors.append([case,proc.returncode,proc.stderr]);continue
 x=json.loads(f.read_text());rows.append(x)
 try:
  assert proc.returncode==0 and x['worker_joined'] and x['closed_cleanly'],x.get('failure')
  assert not x['failure'] and not x['stalled'] and not x['worker_busy'] and not x['pending'],x.get('failure')
  events=x['delivered'];indexes=[n['index'] for n in events if n['kind']=='index'];done=[n for n in events if n['kind']=='done']
  assert all(n['drained'] for n in x['notifications'] if n['kind']=='done'),'premature DONE before drain'
  if case=='sayall':assert not x['reader_running'] and len(done)==20 and len(indexes)==39
  elif case=='silent':assert indexes==[0,0,777] and len(done)==2 and [n['kind'] for n in events]==['index']*3+['done']*2
  elif case=='cancel':assert x['controlled'] and indexes==[779] and len(done)==1
  elif case=='queued':assert indexes==[777,778] and len(done)==2
  else:assert indexes==[777] and len(done)==1
  if case not in ('cancel',):
   assert x['generated_frames']==x['consumed_frames']==x['fed_frames'],'missing PCM'
   active=[q for q in x['players'] if q['sent']]
   if active:
    assert len(active)==1 and active[0]['sha256']==x['generated_sha256'],'PCM digest'
    assert active[0]['tail']==x['native_tail'],'missing tail'
  assert all(q['max_chunk']<=2048 and q['max_padding']<=round(q['rate']*.4) for q in x['players'])
 except AssertionError as ex:errors.append([case,str(ex)])
 print(case,'PASS' if not any(e[0]==case for e in errors) else 'FAIL',x['fed_frames'],flush=True)
(a.output/'results.json').write_text(json.dumps(rows,indent=2));(a.output/'acceptance.json').write_text(json.dumps(dict(cases=len(cases),collected=len(rows),errors=errors,passed=not errors),indent=2))
assert len(rows)==len(cases) and not errors,errors
print('PASS',len(rows),'fresh caller-contract cases')
