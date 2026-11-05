"""Compare full 22-mode PCM with an external known-good private DLL.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
from contract import Engine, Mode

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--dll',type=Path,required=True)
p.add_argument('--reference-dll',type=Path,required=True)
p.add_argument('--resources-dir',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
assert hashlib.sha256(a.reference_dll.read_bytes()).hexdigest()=='0c58dcf30aa362419ce5b78de8db5e26e0d153e6e7ed1a4d10e98ce91c6cddd7'
results=[]
for path in (a.dll,a.reference_dll):
    e=Engine(path.resolve(),a.resources_dir.resolve());rows=[]
    try:
        modes=(Mode*64)();n=e.list_modes(e.data,modes,64);assert n==22
        for m in modes[:n]:
            name=m.name.decode('cp1252');e.voice(name)
            actual=e.render(['The quick brown fox jumps over the lazy dog, and then it runs away into the forest.'])
            assert actual['result']==0 and actual['frames']>0
            rows.append(dict(voice=name,sample_rate=m.sample_rate,**actual))
    finally:e.finish()
    results.append(rows)
assert results[0]==results[1],'Full PCM/frames/rate differs from exact known-good DLL'
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(dict(modes=len(results[0]),matched=True,rows=results[0]),indent=2))
print('PASS full PCM/frames/rate 22 modes vs exact 0.5.2 private native DLL')
