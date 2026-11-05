"""Exact external rejected-driver RED; never vendor the rejected source.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--rejected-root',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
source=a.rejected_root/'synthDrivers/msttsSapi4.py'
q=subprocess.run([sys.executable,'-B',str(Path(__file__).with_name('exercise.py')),
                  '--target-root',str(a.rejected_root.resolve()),'--output',str(a.output.resolve()),
                  '--case','10s','--budget','3'],capture_output=True,text=True,timeout=40)
(a.output/'negative.log').write_text(q.stdout+'\n'+q.stderr)
x=json.loads((a.output/'p5-caller-10s.json').read_text())
assert q.returncode==0 and x['stalled'] and x['cancel_released_worker'] and x['worker_joined'],x
assert x['generated_frames']!=x['consumed_frames'] or x['pending'] or x['worker_busy']
report=dict(expected_rejection_observed=True,synthetic_mutation=False,
            rejected_driver_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            stalled=x['stalled'],cancel_released_worker=x['cancel_released_worker'])
(a.output/'negative.json').write_text(json.dumps(report,indent=2));print('PASS exact external rejected-driver RED',report)
