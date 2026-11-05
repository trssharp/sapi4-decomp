# SPDX-License-Identifier: GPL-2.0-or-later
"""Oversized native call is rejected without PCM/marks; worker request then recovers."""
from pathlib import Path
import sys,time,json
E=Path(__file__).resolve().parent;s=(E/'exercise.py').read_text();exec(compile(s[:s.index('# Actual TextReader')],str(E/'exercise.py'),'exec'))
nativeCall=d._native.speak_fragments;checks=[];checked=False
@mod.bridge.PCM
def forbiddenPCM(ptr,n,u):checks.append(('unexpected_pcm',n));return 0
@mod.bridge.MARK
def forbiddenMark(i,f,u):checks.append(('unexpected_marker',i));return 0
def guarded(h,parts,n,m,mu,pcm,pu):
 global checked
 if not checked:
  checked=True
  for size in (65536,100000):
   a=(mod.bridge.Fragment*1)(mod.bridge.Fragment(0,0,b'a'*size));r=raw(h,a,1,forbiddenMark,None,forbiddenPCM,None)
   assert r==-1 and not checks,(size,r,checks)
   checks.append(dict(bytes=size,result=r,no_pcm_or_markers=True));saved=checks.pop();results.append(saved)
 return nativeCall(h,parts,n,m,mu,pcm,pu)
results=[];d._native.speak_fragments=guarded
try:
 d.speak(['Recovery after oversized native rejection.',IndexCommand(777)])
 end=time.monotonic()+8
 while not any(x['kind']=='done' for x in delivered):
  queueHandler.pumpAll();assert time.monotonic()<end and not d._failure,d._failure;time.sleep(.001)
 assert [x['index'] for x in delivered if x['kind']=='index']==[777]
finally:
 mod.BaseSynthDriver.terminate=lambda self:None;d.terminate()
assert d._closedCleanly and not d._thread.is_alive()
(output/'limits.json').write_text(json.dumps(dict(rejections=results,recovery=True),indent=2));print('PASS',results,'recovery')
