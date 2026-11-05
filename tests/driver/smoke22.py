"""All-mode isolated no-device driver smoke. SPDX-License-Identifier: GPL-2.0-or-later"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--target-root',type=Path,required=True)
p.add_argument('--native-report',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
voices=json.loads(a.native_report.read_text())['voices22'];assert len(voices)==22
rows=[]
for i,v in enumerate(voices):
    folder=a.output/str(i)
    q=subprocess.run([sys.executable,'-B',str(Path(__file__).with_name('exercise.py')),
                      '--target-root',str(a.target_root.resolve()),'--output',str(folder.resolve()),
                      '--case','smoke','--voice',v['voice']],capture_output=True,text=True,timeout=35)
    (a.output/(str(i)+'.log')).write_text(q.stdout+'\n'+q.stderr)
    x=json.loads((folder/'p5-caller-smoke.json').read_text())
    assert q.returncode==0 and not x['stalled'] and not x['failure'] and x['worker_joined'] and x['closed_cleanly']
    assert x['generated_frames']==x['fed_frames']==x['consumed_frames']>0
    active=[v for v in x['players'] if v['sent']]
    assert len(active)==1 and active[0]['sha256']==x['generated_sha256'] and active[0]['tail']==x['native_tail']
    assert [v['index'] for v in x['delivered'] if v['kind']=='index']==[777]
    assert len([v for v in x['delivered'] if v['kind']=='done'])==1
    rows.append(dict(voice=v['voice'],frames=x['fed_frames'],sha256=x['generated_sha256']))
    print(v['voice'],'PASS',flush=True)
(a.output/'report.json').write_text(json.dumps(dict(passed=True,count=len(rows),rows=rows),indent=2))
print('PASS 22-mode no-device driver PCM smoke')
