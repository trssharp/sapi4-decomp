# License scope and NVDA attribution

The upstream root LICENSE remains the MIT license, Copyright (c) 2026 Quinton
Williams. It continues to apply to upstream source and the canonical native C
boundary, ownership/shutdown code, native translation-unit modifications and C
test clients added here. Nothing in this notice relicenses upstream history or
claims rights in Microsoft engine binaries, voice files, recordings or SDK data.
None of those assets are included.

The `nvda/` Python driver/bridge, `tests/driver/`, added Python build/test/audit
tools, Python native test adapters, authored text/hash fixtures and new
integration documentation are distributed under GPL-2.0-or-later. A copy of
GNU GPL version 2 is in GPL-2.0.txt. You may elect that version or any later
version. These additions are provided without warranty, including the implied
warranties of merchantability or fitness for a particular purpose.

The two driver files are preserved byte-for-byte from the working 0.5.2 source;
this adjacent notice supplies their license declaration without altering their
verified source hashes. Their implementation uses NVDA public synth-driver,
speech-command, notification, configuration and WavePlayer interfaces. The
stock tagged-sequence formation and the source-derived no-device fixtures were
reviewed against NVDA source, not represented as a new independent MIT
implementation of NVDA internals.

NVDA source reference revision:
`84756356cd19a57cfeb4ef265619e54850e06568` at
https://github.com/nvaccess/nvda . Relevant references:

* `source/_synthDrivers32/sapi4.py`: Copyright (C) 2006-2026 NV Access Limited,
  Leonard de Ruijter, gexgd0419. Stock SAPI4 sequence/marker contract reference.
* `source/nvwave.py`: Copyright (C) 2007-2025 NV Access Limited, Aleksey Sadovoy,
  Cyrille Bougot, Peter Vágner, Babbage B.V., Leonard de Ruijter, James Teh.
  `nvdaHelper/local/wasapi.cpp` and nvwave.py establish the caller-dispatched
  feed/sync callback/backpressure contract modeled by the test Player.
* `source/speech/sayAll.py`, `source/speech/manager.py`,
  `source/speech/speechWithoutPauses.py`, `source/NVDAObjects/__init__.py`,
  `source/textInfos/offsets.py`: Copyright NV Access Limited and contributors.
  The bootstrap/provider fixtures use those classes; the external compiled
  runtime executes the actual SpeechManager/TextReader implementations. The
  repository does not vendor the NVDA core or its compiled modules.

Authoritative NVDA terms and special exceptions are recorded in NVDA's
[copying.txt at the reference revision](https://github.com/nvaccess/nvda/blob/84756356cd19a57cfeb4ef265619e54850e06568/copying.txt).
The added code here elects GPL-2.0-or-later and does not assert that any optional
NVDA exception authorizes Microsoft voice-asset redistribution. The standard
GPL license text was obtained from
https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt .

Text/hash fixture provenance: golden46 inputs/control values and expected PCM
hashes are retained local verification exports of the upstream 46-job corpus;
original130 inputs/control values and hashes are sanitized retained original-
engine comparison exports, filtered to the inline05 cases. Only authored input
text and non-reversible digest metadata are included, not WAV/PCM or derived
engine tables. Passing tests mean fresh reconstructed PCM matches those retained
references, not that an original-engine emulator was newly run.
