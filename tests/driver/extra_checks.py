# SPDX-License-Identifier: GPL-2.0-or-later
"""Blocking drain proof and real SpeechManager interruption of real TextReader."""
from pathlib import Path
import sys,time,json,threading
E=Path(__file__).resolve().parent;s=(E/'exercise.py').read_text();exec(compile(s[:s.index('# Actual TextReader')],str(E/'exercise.py'),'exec'))
def wait(f,seconds=8,pump=True):
 end=time.monotonic()+seconds
 while not f():
  if pump:queueHandler.pumpAll()
  assert time.monotonic()<end,('timeout',d._failure)
  time.sleep(.001)
report={}
try:
 # Block exactly the final sync/idle boundary; DONE must not be emitted early.
 entered=threading.Event();release=threading.Event();oldSync=Player.sync
 def sync(p):entered.set();assert release.wait(8);oldSync(p)
 Player.sync=sync
 d.speak(['Drain boundary tail.',IndexCommand(9000)]);wait(entered.is_set)
 queueHandler.pumpAll();assert not any(x['kind']=='done' for x in delivered)
 release.set();wait(lambda:any(x['kind']=='done' for x in delivered));Player.sync=oldSync
 assert [x['index'] for x in delivered if x['kind']=='index']==[9000]
 report['blocked_idle_no_premature_done']=True
 # Queue direct fixture only after clearing SpeechManager state using its real public cancel.
 d.cancel();Player.release.clear();Player.blocked.clear();Player.hold=True
 from config.featureFlag import FeatureFlag
 from config.featureFlagEnums import SayAllReadingUnitFlag as RU
 config.conf['speech']['sayAllReadingUnit']=FeatureFlag(RU.LINE,RU.SENTENCE)
 corpus='\n'.join([text.strip()]*18+['END_SENTINEL.'])
 ends=[m.end() for m in re.finditer(r'\n|$',corpus) if m.end()>0]
 obj=Obj(text=corpus,ends=ends);api.getCaretObject=lambda:obj;api.isCursorManager=lambda obj:False
 sayAll.systemUtils.preventSystemIdle=lambda **kw:None;sayAll.systemUtils.resetThreadExecutionState=lambda:None
 sayAll.objectBelowLockScreenAndWindowsIsLocked=lambda obj:False;config.conf['reviewCursor']['followCaret']=False
 sayAll.initialize(speech.speak,speech.speakObject,speech.getTextInfoSpeech,speech.SpeakTextInfoState)
 sayAll.SayAllHandler.readText(sayAll.CURSOR.CARET);wait(Player.blocked.is_set)
 assert sayAll.SayAllHandler.isRunning()
 # NVDA navigation path stops Say All then cancels speech; manager must recover.
 sayAll.SayAllHandler.stop();before=len(delivered);start=time.monotonic();speech.cancelSpeech();stop=time.monotonic()-start
 Player.hold=False;Player.release.set();speech.speak(['Navigation recovery.'])
 wait(lambda:any(x['kind']=='done' for x in delivered[before:]))
 assert not sayAll.SayAllHandler.isRunning() and not d._failure
 assert len([x for x in delivered[before:] if x['kind']=='done'])==1
 report.update(real_textreader_speechmanager_interrupt_recovery=True,stop_call_s=stop,worker_settled=not d._busy)
finally:
 Player.hold=False;Player.release.set();mod.BaseSynthDriver.terminate=lambda self:None;d.terminate()
assert d._closedCleanly and not d._thread.is_alive()
(output/'extra-checks.json').write_text(json.dumps(report,indent=2));print('PASS',report)
