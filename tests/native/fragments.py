"""Malformed fragment and held-PCM cross-thread cancellation contracts.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import threading
from contract import Engine, Fragment, PCM, MARK
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--dll',type=Path,required=True)
p.add_argument('--resources-dir',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();e=Engine(a.dll.resolve(),a.resources_dir.resolve());results=[]
try:
    e.voice('Sam');callbacks=[]
    @PCM
    def forbidden_pcm(ptr,n,u):callbacks.append(('pcm',n));return 0
    @MARK
    def forbidden_mark(i,f,u):callbacks.append(('mark',i));return 0
    bad=[(Fragment(99,0,None),1),(Fragment(0,0,None),1),
         (Fragment(1,2147483648,None),1),(Fragment(1,0,b'bad'),1),
         (Fragment(0,0,b'a'*65536),1),(Fragment(0,0,b'\\'*32768),1)]
    for f,n in bad:
        arr=(Fragment*1)(f)
        assert e.speak_fragments(e.e,arr,n,forbidden_mark,None,forbidden_pcm,None)==-1 and not callbacks
    assert e.speak_fragments(e.e,None,1,forbidden_mark,None,forbidden_pcm,None)==-1
    assert e.speak_fragments(e.e,None,4097,forbidden_mark,None,forbidden_pcm,None)==-1
    results.append(dict(invalid_batches=8,no_pcm_or_markers=True))
    text='The gardener carried plants into the greenhouse and watered them before sunset. '*3
    full=e.render([text,999]);assert full['result']==0
    for target in (1,full['frames']//2,full['frames']):
        e.voice('Sam');entered=threading.Event();release=threading.Event();frames=0;events=[];state={}
        @PCM
        def pcm(ptr,n,u):
            global frames
            frames+=n
            if frames>=target and not entered.is_set():
                entered.set()
                if not release.wait(10):state['callback_timeout']=True;return 1
            return 0
        @MARK
        def mark(i,f,u):events.append(i);return 0
        arr=(Fragment*2)(Fragment(0,0,text.encode('cp1252')),Fragment(1,999,None))
        def work():state['result']=e.speak_fragments(e.e,arr,2,mark,None,pcm,None)
        th=threading.Thread(target=work);th.start()
        assert entered.wait(15)
        before=list(events);e.stop(e.e);release.set();th.join(15)
        assert not th.is_alive() and state.get('result')==1 and not state.get('callback_timeout')
        assert events==before,'marker emitted after stop handshake'
        recovery=e.render(['Recovery.',123]);assert recovery['result']==0 and recovery['frames']>0 and recovery['events'][-1][0]==123
        results.append(dict(target_frame=target,canceled=True,recovery=True))
finally:e.finish()
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(results,indent=2))
print('PASS 8 malformed batches; early/middle/final held-PCM stop and recovery')
