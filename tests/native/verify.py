"""Resource-provisioned native tests; no audio device. SPDX-License-Identifier: GPL-2.0-or-later"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import wave
from contract import Engine, Mode
from contract import decode
ROOT = Path(__file__).resolve().parents[2]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def pe_info(path):
    b=path.read_bytes(); u16=lambda i:struct.unpack_from('<H',b,i)[0]; u32=lambda i:struct.unpack_from('<I',b,i)[0]
    pe=u32(60); assert b[pe:pe+4]==b'PE\0\0'; coff=pe+4; opt=coff+20
    assert u16(coff)==0x8664 and u16(opt)==0x20b
    sections=[]
    for i in range(u16(coff+2)):
        o=opt+u16(coff+16)+40*i; sections.append((u32(o+12),u32(o+8),u32(o+20),u32(o+16)))
    def offset(rva):
        for va,vs,raw,size in sections:
            if va<=rva<va+max(vs,size): return raw+rva-va
        raise ValueError(rva)
    def string(o): return b[o:b.index(b'\0',o)].decode('ascii')
    ex=u32(opt+112); names=[]
    if ex:
        o=offset(ex); count=u32(o+24); table=offset(u32(o+32))
        names=[string(offset(u32(table+4*i))) for i in range(count)]
    imports=[]; imp=u32(opt+120)
    if imp:
        o=offset(imp)
        while any(b[o:o+20]): imports.append(string(offset(u32(o+12)))); o+=20
    return dict(machine='0x8664',exports=sorted(names),imports=imports,sha256=sha(path))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--resources-dir',type=Path,default=os.environ.get('SAPI4_RESOURCES_DIR'))
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if not a.resources_dir: p.error('--resources-dir or SAPI4_RESOURCES_DIR required')
    build=a.build.resolve(); data=a.resources_dir.resolve(); out=a.output.resolve()
    out.mkdir(parents=True,exist_ok=False); dll=build/'MSTTSSapi4.dll'
    hashes=dict(re.findall(r'\{"([^"]+)","([0-9a-f]{64})"\}',(ROOT/'native/resource_hashes.h').read_text()))
    before={n:sha(data/n) for n in hashes}; assert before==hashes and len(hashes)==11
    report={'dll_sha256':sha(dll),'physical_playback':False,'strict_memory_qualification':'BLOCKED, not waived'}
    def save(): (out/'report.json').write_text(json.dumps(report,indent=2))
    def run(name,cmd,env=None):
        q=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=540,env=env)
        (out/(name+'.log')).write_text(q.stdout+'\nSTDERR\n'+q.stderr)
        assert q.returncode==0,(name,q.returncode,q.stdout[-1000:],q.stderr[-1000:])
        print(name,'PASS',flush=True); return q.stdout
    pe=pe_info(dll)
    exports=re.findall(r'^\s+(s4_\w+)\s*$',(ROOT/'native/sapi4tts.def').read_text(),re.M)
    assert len(exports)==19 and pe['exports']==sorted(exports)
    assert 'msttssyn.dll' not in [n.lower() for n in pe['imports']]
    report['pe']=pe;save()
    run('standalone',[build/'standalone_test.exe',dll,data])
    run('lifecycle1000',[build/'lifecycle_test.exe',dll,data,1000])
    run('lifecycle-mixed1000',[build/'lifecycle_test.exe',dll,data,1000,'mixed'])
    run('live-capacity',[build/'live_capacity_test.exe',dll,data])
    run('sample-client',[build/'sample_client.exe',dll,data,'Sam','Hello. Native sample client.',out/'sample.pcm'])
    run('failed-setup1000',[build/'failed_setup_test.exe',build/'fault.dll',data])
    for name in ('alternate','invalid'):
        folder=out/name;folder.mkdir()
        for n in hashes: shutil.copyfile(data/n,folder/n)
    (out/'invalid/msttssyn.dll').write_bytes(b'intentionally invalid isolated test fixture')
    run('shutdown',[build/'shutdown_test.exe',dll,data,out/'alternate',out/'invalid'])
    run('shutdown-fault400',[build/'shutdown_test.exe',build/'fault.dll',data,out/'alternate',out/'invalid','fault'])
    report['native_controls']=['standalone45','lifecycle1000','mixed1000','live-capacity126','sample-client','failed-setup1000','shutdown','shutdown-fault400'];save()
    rows=json.loads((ROOT/'tests/fixtures/golden46.json').read_text());assert len(rows)==46
    for threads in (1,4):
        folder=out/f'golden{threads}';folder.mkdir();jobs=folder/'jobs.txt'
        jobs.write_text('\n'.join('|'.join(map(str,[folder/r['name'],data,r['mode'],0,r['pitch'],r['speed'],1,r['text']])) for r in rows)+'\n')
        run('golden'+str(threads),[build/'api_check_dynamic.exe',data/'msttssyn.dll',jobs,threads],dict(os.environ,S4_PREVIEW_DLL=str(dll)))
        for r in rows:
            with wave.open(str(folder/r['name']),'rb') as w: n=w.getnframes();pcm=w.readframes(n)
            assert n==r['frames'] and hashlib.sha256(pcm).hexdigest()==r['pcm_sha256'],r['name']
        report['golden_threads'+str(threads)]=len(rows);save()
    e=Engine(dll,data)
    try:
        modes=(Mode*64)();count=e.list_modes(e.data,modes,64);assert count==22
        voices=[]
        for m in modes[:count]:
            name=m.name.decode('cp1252');e.voice(name);x=e.render(['The quick brown fox jumps over the lazy dog, and then it runs away into the forest.'])
            assert x['result']==0 and x['frames']>0;voices.append(dict(voice=name,**x))
        report['voices22']=voices;save()
        refs=json.loads((ROOT/'tests/fixtures/original130.json').read_text());assert len(refs)==130
        for x in refs:
            e.voice(x['voice'])
            for n in ('speed','pitch'):
                if x[n]: assert getattr(e,'set_'+n)(e.e,x[n])==0
            parts=decode(x['text']) if x['flags'] else [x['text']]
            actual=e.render(parts)
            assert actual['result']==0 and actual['pcm_sha256']==x['pcm_sha256'],(x['id'],actual)
            assert [v[0] for v in actual['events']]==[v for v in parts if isinstance(v,int)],x['id']
        report['original_reference_pcm_and_markers']=len(refs);save()
    finally: e.finish()
    assert {n:sha(data/n) for n in hashes}==before
    report['external_resources_unchanged']=11;report['passed']=True;save()
    print('PASS native controls, 46 goldens one/four threads, 22 voices, 130 original-reference PCM/marker cases')

if __name__=='__main__':main()
