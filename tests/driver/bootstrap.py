# SPDX-License-Identifier: GPL-2.0-or-later
"""Load caller-provided compiled NVDA without starting NVDA or an audio device.
Source-derived TextInfo fixture using NV Access NVDA 84756356 public APIs.
Copyright NV Access Limited and contributors; see LICENSES/NVDA-NOTICE.md.
"""
import os
from pathlib import Path
import sys


def initialize(output):
    runtime = os.environ.get('NVDA_RUNTIME')
    if not runtime:
        raise RuntimeError('Set NVDA_RUNTIME to an external compiled NVDA runtime (Python 3.13)')
    if sys.version_info[:2] != (3, 13):
        raise RuntimeError('Use Python 3.13 for the compiled NVDA fixture')
    r = Path(runtime).resolve()
    assert (r/'library.zip').is_file()
    global _dll_directory
    _dll_directory = os.add_dll_directory(str(r))
    sys.path[:0] = [str(r/'library.zip'), str(r)]
    import globalVars
    globalVars.appArgs.configPath = str(output/'isolated-config')
    globalVars.appArgs.disableAddons = True
    globalVars.appDir = str(r)
    (output/'isolated-config').mkdir(exist_ok=True)
    import NVDAState
    NVDAState.isRunningAsSource = lambda: False
    import config
    config.initialize()
    import languageHandler
    languageHandler.setLanguage('en')
    # Resource projection only. Never execute this file or select/install a synth.
    sys.executable = str(r/'nvda.exe')
    import monkeyPatches.comtypesMonkeyPatches as cmp
    cmp.replace_check_version()
    cmp.appendComInterfacesToGenSearchPath()
    import certifi
    certifi.where = lambda: str(r/'certifi/cacert.pem')
    import appModuleHandler  # Establish NVDA's supported bootstrap import order.
    import characterProcessing
    characterProcessing.initialize()
    import speech
    from speech import sayAll
    from speech.manager import SpeechManager
    from speech.speechWithoutPauses import SpeechWithoutPauses
    import wx
    assert wx.GetApp() is None

# Provider classes are loaded only after initialize: real NVDA base classes,
# fixture endpoints/caret policy, not a live application/provider claim.
def provider():
    from NVDAObjects import NVDAObject, NVDAObjectTextInfo
    from textInfos.offsets import Offsets
    import textInfos
    class TI(NVDAObjectTextInfo):
        def _getStoryText(self): return self.obj.basicText
        def _getStoryLength(self): return len(self.obj.basicText)
        def _getCaretOffset(self): return 0
        def _getSelectionOffsets(self): return (0, 0)
        def updateCaret(self): pass
        def getTextWithFields(self, formatConfig=None):
            return super().getTextWithFields(formatConfig)
        def move(self, unit, direction, endPoint=None):
            if unit != textInfos.UNIT_READINGCHUNK:
                return super().move(unit, direction, endPoint)
            old = self._endOffset
            nxt = next((x for x in self.obj.ends if x > old), old)
            self._endOffset = nxt
            return int(nxt > old)
    class Obj(NVDAObject):
        processID = None
        TextInfo = TI
        def __init__(self, text, ends):
            super().__init__(); self.basicText = text; self.ends = ends
        def _isEqual(self, other): return self is other
        def makeTextInfo(self, pos):
            if pos == textInfos.POSITION_CARET: pos = Offsets(0, 0)
            return self.TextInfo(self, pos)
    return Obj
