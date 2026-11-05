# SPDX-License-Identifier: GPL-2.0-or-later
# Source-derived NVDA fixture; see LICENSES/NVDA-NOTICE.md.
"""Faithful no-device caller-dispatch acceptance; optional exact target root."""
from pathlib import Path
import sys,os,time,json,threading,hashlib,types,importlib,ctypes,traceback,argparse
parser=argparse.ArgumentParser();parser.add_argument('--target-root',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--case',default='10s');parser.add_argument('--delivery',default='caller');parser.add_argument('--version',default='p5');parser.add_argument('--voice');parser.add_argument('--budget',type=float,default=8)
a=parser.parse_args();target=a.target_root;output=a.output;output.mkdir(exist_ok=True,parents=True);case=a.case;delivery=a.delivery;version=a.version;voice=a.voice;budget=a.budget
# No artifact imports: explicit external runtime, isolated no-device state.
from bootstrap import initialize, provider
initialize(output)
OUT=output
Obj=provider()
import config, queueHandler, synthDriverHandler, api, re, wx
import speech
from speech import sayAll
from speech.manager import SpeechManager
import nvwave
from speech.commands import IndexCommand
import speech.speech as sp
class Player:
 registry=[];hold=False;blocked=threading.Event();release=threading.Event()
 def __init__(self,**kw):
  self.rate=kw['samplesPerSec'];self.bufferFrames=round(self.rate*.4);self.sent=0;self.playpos=0;self.completed=0;self.callbacks=0;self.q=[];self.buffers=[];self.digest=hashlib.sha256();self.consumed=hashlib.sha256();self.consumedFrames=0;self.maxPadding=0;self.maxChunk=0;self.maxCallbacks=0;self.stale=[];self.epoch=0;self.paused=False;self.stops=0;self.idles=0;self.closed=False;self.trace=[];self.lock=threading.RLock();self.first=None;self.tail=b''
  self.registry.append(self)
 def advance(self,pos):
  self.playpos=pos
  while self.buffers and self.buffers[0][0]<=pos:
   end,data=self.buffers.pop(0);self.consumed.update(data);self.consumedFrames+=len(data)//2;self.tail=(self.tail+data)[-128:]
 def poll(self):
  while self.q and self.q[0][0]<=self.playpos:
   frame,cb=self.q.pop(0);self.completed=frame;self.callbacks+=1
   if cb:cb()
 def feed(self,data,onDone=None):
  epoch=self.epoch
  if Player.hold and data and self.sent-self.playpos>self.bufferFrames//2:
   Player.blocked.set();assert Player.release.wait(10),'blocked feed not woken'
   if epoch!=self.epoch:return
  with self.lock:
   if data and self.first is None:self.first=time.monotonic()
   count=len(data)//2;self.maxChunk=max(self.maxChunk,count)
   assert count<=2048 or version=='p4'
   if delivery=='unpaced':self.advance(self.sent)
   elif self.sent-self.playpos>self.bufferFrames//2:
    advance=self.sent-self.bufferFrames//2
    if self.q:advance=min(advance,max(self.playpos,self.q[0][0]))
    self.advance(advance)
   # Each tested P5 call is <=2048, so sufficient room after half-buffer wait.
   # P4 feeds whole native chunks; consume in pieces using the same native bound.
   offset=0
   while offset<len(data):
    room=self.bufferFrames-(self.sent-self.playpos)
    if not room:self.advance(self.sent-self.bufferFrames//2);room=self.bufferFrames-(self.sent-self.playpos)
    n=min(room*2,len(data)-offset);piece=data[offset:offset+n];self.sent+=n//2;self.buffers.append((self.sent,piece));offset+=n
    self.maxPadding=max(self.maxPadding,self.sent-self.playpos)
    self.poll()
   self.poll();self.digest.update(data)
   if onDone:self.q.append((self.sent,onDone))
   self.maxCallbacks=max(self.maxCallbacks,len(self.q))
   if delivery=='independent':self.advance(self.sent);self.poll()
 def sync(self):
  with self.lock:self.advance(self.sent);self.poll()
 def idle(self):self.sync();self.idles+=1
 def stop(self):
  with self.lock:
   self.stale.extend(cb for _,cb in self.q if cb);self.q.clear();self.buffers.clear();self.playpos=self.sent;self.epoch+=1;self.stops+=1;self.paused=False
  Player.release.set()
 def pause(self,switch):self.paused=switch
 def close(self):self.stop();self.closed=True
nvwave.WavePlayer=Player
addon=target.resolve()
assert (addon/'synthDrivers/msttsSapi4.py').is_file(),addon
pkg=types.ModuleType('diagnosticDriver');pkg.__path__=[str(addon/'synthDrivers')];sys.modules[pkg.__name__]=pkg
mod=importlib.import_module('diagnosticDriver.msttsSapi4')
# Avoid BaseSynthDriver persistence/config transactions: initialize only its actual worker-owned state.
original=mod.BaseSynthDriver.__init__;mod.BaseSynthDriver.__init__=lambda self:None
d=mod.SynthDriver();mod.BaseSynthDriver.__init__=original
synthDriverHandler._curSynth=d
manager=SpeechManager();sp._manager=manager;sp.initialize()
notifications=[];submissions=[];native=[];delivered=[]
def recordIndex(synth,index):
 if synth is d:delivered.append(dict(kind='index',index=index,t=time.monotonic()))
def recordDone(synth):
 if synth is d:delivered.append(dict(kind='done',t=time.monotonic(),drained=all(not p.buffers and not p.q for p in Player.registry)))
synthDriverHandler.synthIndexReached.register(recordIndex)
synthDriverHandler.synthDoneSpeaking.register(recordDone)
oldSpeak=d.speak
text=('The gardener carried plants into the greenhouse and watered them before sunset. ')
reps={'short':0,'10s':3,'30s':9,'60s':18,'1kb':13,'10kb':130,'sayall':18,'queued':3,'100kb':1300,'silent':0,'cancel':130,'pause':18,'smoke':0}[case]
corpus=('Hi.' if case in ('short','smoke') else text*reps+' END_SENTINEL.')
if voice:d._set_voice(voice)
def tracedSpeak(seq):
 submissions.append(dict(t=time.monotonic(),sequence=[x if isinstance(x,str) else repr(x) for x in seq],busy=d._busy,pending=len(d._pending)))
 return oldSpeak(seq)
d.speak=tracedSpeak
oldNotify=d._notify
def notify(g,n,**kw):
 notifications.append(dict(t=time.monotonic(),kind='index' if 'index' in kw else 'done',index=kw.get('index'),thread=threading.current_thread().name,drained=all(not p.buffers and not p.q for p in Player.registry)))
 return oldNotify(g,n,**kw)
d._notify=notify
raw=d._native.speak_fragments if version=='p5' else d._native.speak
generated=hashlib.sha256();generatedFrames=0;nativeTail=b''
def tracedNative(*args):
 row=dict(start=time.monotonic());native.append(row)
 if version=='p5':
  handle,parts,n,mark,mu,originalPCM,pu=args
  row['fragments']=[dict(kind=x.kind,index=x.index,text=x.text.decode('cp1252') if x.text else None) for x in parts]
  @mod.bridge.PCM
  def tee(ptr,count,user):
   global generatedFrames,nativeTail
   data=ctypes.string_at(ptr,count*2);generated.update(data);generatedFrames+=count;nativeTail=(nativeTail+data)[-128:]
   return originalPCM(ptr,count,user)
  result=raw(handle,parts,n,mark,mu,tee,pu)
 else:result=raw(*args)
 row.update(end=time.monotonic(),result=result);return result
if version=='p5':d._native.speak_fragments=tracedNative
else:d._native.speak=tracedNative
canonical=None
# Actual TextReader/SpeechWithoutPauses/SpeechManager and event queue, no fake notification completion.
if case=='sayall':
 from config.featureFlag import FeatureFlag
 from config.featureFlagEnums import SayAllReadingUnitFlag as RU
 config.conf['speech']['sayAllReadingUnit']=FeatureFlag(RU.LINE,RU.SENTENCE)
 corpus='\n'.join([text.strip()]*reps+['END_SENTINEL.'])
 ends=[m.end() for m in re.finditer(r'\n|$',corpus) if m.end()>0]
 obj=Obj(text=corpus,ends=ends)
 api.getCaretObject=lambda:obj;api.isCursorManager=lambda obj:False
 sayAll.systemUtils.preventSystemIdle=lambda **kw:None
 sayAll.systemUtils.resetThreadExecutionState=lambda:None
 sayAll.objectBelowLockScreenAndWindowsIsLocked=lambda obj:False
 config.conf['reviewCursor']['followCaret']=False
 sayAll.initialize(speech.speak,speech.speakObject,speech.getTextInfoSpeech,speech.SpeakTextInfoState)
 sayAll.SayAllHandler.readText(sayAll.CURSOR.CARET)
elif case=='silent':
 d.speak([IndexCommand(0),IndexCommand(0),IndexCommand(777)]);d.speak([])
else:
 if case in ('cancel','pause'):Player.hold=True
 d.speak([corpus,IndexCommand(777)])
 if case=='cancel':d.speak(['Must be discarded.',IndexCommand(778)])
start=time.monotonic();snapshots=[];controlled=False;cancelCall=None;beforeCancel=None
while time.monotonic()-start<budget:
 queueHandler.pumpAll();time.sleep(.002)
 if len(snapshots)<1 or time.monotonic()-snapshots[-1]['t']>.1:
  snapshots.append(dict(t=time.monotonic(),busy=d._busy,pending=len(d._pending),sent=sum(p.sent for p in Player.registry),callbacks=sum(p.callbacks for p in Player.registry),notifications=len(notifications)))
 if case=='queued' and len(submissions)==1 and time.monotonic()-start>.005:d.speak(['Queued navigation.',IndexCommand(778)])
 if case in ('cancel','pause') and Player.blocked.is_set() and not controlled:
  controlled=True
  if case=='cancel':
   beforeCancel=len(notifications);t=time.monotonic();d.cancel();cancelCall=time.monotonic()-t
   for p in Player.registry:
    for cb in p.stale:cb()
   # Pump already queued notification deliveries only after generation invalidation.
   queueHandler.pumpAll()
   Player.hold=False;Player.release.set();d.speak(['Recovery.',IndexCommand(779)])
  else:
   d.pause(True);time.sleep(.02);assert not any(n['kind']=='done' for n in notifications)
   d.pause(False);Player.hold=False;Player.release.set()
 if submissions and not d._busy and not d._pending and (case!='sayall' or not sayAll.SayAllHandler.isRunning()):break
stalled=d._busy and not any('end' in n for n in native)
frames=sum(p.sent for p in Player.registry)
queueHandler.pumpAll()
row=dict(delivered=delivered,version=version,delivery=delivery,case=case,input_bytes=len(corpus.encode()),elapsed=time.monotonic()-start,stalled=stalled,worker_busy=d._busy,pending=len(d._pending),failure=d._failure,canonical=canonical,fed_frames=frames,callbacks=sum(p.callbacks for p in Player.registry),queued_callbacks=sum(len(p.q) for p in Player.registry),notifications=notifications,submissions=submissions,native=native,snapshots=snapshots,reader_running=sayAll.SayAllHandler.isRunning() if case=='sayall' else None,liveNVDA=False,audible=False,wx_app_none=wx.GetApp() is None,python=sys.version)
row.update(generated_frames=generatedFrames,generated_sha256=generated.hexdigest(),native_tail=nativeTail.hex(),consumed_frames=sum(p.consumedFrames for p in Player.registry),players=[dict(rate=p.rate,sent=p.sent,consumed=p.consumedFrames,sha256=p.consumed.hexdigest(),tail=p.tail.hex(),max_padding=p.maxPadding,max_chunk=p.maxChunk,max_callbacks=p.maxCallbacks,idles=p.idles,first=p.first) for p in Player.registry],cancel_call_s=cancelCall,controlled=controlled,voice=voice)
# Capture the blocked stack before bounded cancellation, without touching any live process.
if stalled:
 import sys as _sys
 stack=_sys._current_frames().get(d._thread.ident)
 row['blocked_stack']=''.join(traceback.format_stack(stack))
 if case=='sayall':sayAll.SayAllHandler.stop()
 t=time.monotonic();d.cancel();row['cancel_seconds']=time.monotonic()-t
 for _ in range(1000):
  queueHandler.pumpAll()
  if not d._busy:break
  time.sleep(.001)
 row['cancel_released_worker']=not d._busy
# Exact native library cleanup; bypass only Base terminate config persistence.
mod.BaseSynthDriver.terminate=lambda self:None
d.terminate();row['worker_joined']=not d._thread.is_alive();row['closed_cleanly']=d._closedCleanly
queueHandler.pumpAll()
(OUT/('-'.join((version,delivery,case))+'.json')).write_text(json.dumps(row,indent=2))
print(json.dumps({k:v for k,v in row.items() if k not in ('snapshots','submissions','native','notifications','blocked_stack')}))
