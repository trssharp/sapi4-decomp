# Experimental native x64 engine and NVDA source

This development branch preserves upstream history, MIT LICENSE and original
emulator/static-library source. It adds one Windows CFB file-loading fix and a
canonical `native/` Windows x64 boundary. `native/core/` contains the seven
reviewed native-only translation-unit overrides; the original emulator continues
to use upstream source. `native/core/sapi4tts.c` is the consolidated final
stock-compatible core, not a generated preview snapshot.

`nvda/` contains **only** manifest.ini and the two Python source modules under
synthDrivers/. No DLLs, voice files, archives, recordings or user settings are
published. Tests/build tools live outside nvda/. There is no release workflow,
GitHub release, attachment, automatic installation or NVDA restart.

## Status and provenance

Experimental preview05 0.5.2, not a stable release. The user reports live 0.5.2
has no cutoff and is overall good; possible choppiness remains under review.
These are user reports, not new audible/playback qualification. The earlier
hours-tested preview04 fallback is not modified.

Exact private baseline archive SHA256:
`dbbfd2e97a44a3bd9793dd587c385d13b96bba95275b70b6cdc3522100f23865`
(16 CRC-checked members, never redistributed here).
Original private native DLL SHA256:
`0c58dcf30aa362419ce5b78de8db5e26e0d153e6e7ed1a4d10e98ce91c6cddd7`.
Tracked driver SHA256:
`73733c98c7c606db0a33b5f0d5d3d7815630f867d4af7011a8ee861db01eced9`.
Tracked bridge SHA256:
`c143d368feefd0b53aa9f6fb2a53760877099d2c2c2ede9f00a72e0d56abea80`.
Both Python modules are byte-identical to that baseline. The manifest name and
version 0.5.2 are preserved. Its docFileName is satisfied only in the explicit
local runtime staging step, by copying this document to readme.md.

The Python-only 0.5.2 callback repair removed pre-feed completion-credit waits.
WavePlayer.feed already provides native bounded backpressure; its completion
callbacks can dispatch inside later feed/sync calls. Waiting for those credits
can starve their dispatcher. No native synthesis tuning is added here.

## Build from source

Requirements: Windows x64; Python 3.13 for the NVDA fixture; a MinGW-w64 x64 GCC
or compatible clang configuration providing winpthreads and bcrypt. Standard
library only for native verification. Compiler path is explicitly configurable,
not tied to a workstation. Commands below are from the repository root:

```
python tools/build_native.py --cc gcc --output .local/build01
python tests/native/verify.py --build .local/build01 --resources-dir "D:/licensed/sapi4" --output .local/native01
```

`CC` is an alternative to --cc. Output must not exist. Every production object
is recompiled from an explicit source inventory (41 translation units); no
preview03 objects, historical command JSON, embedded original-engine data or
voice-baked sources are used. Production and fault-injection DLLs are separate.
The upstream Makefile/emulator remain available; they are not replaced by this
Windows build. Bit-for-bit DLL reproduction across compilers/paths is **not**
claimed; debug paths and toolchain metadata can differ. Complete native PCM
hash identity is the behavioral gate. Repeated builds with the same toolchain
use a fixed preferred PE image base (ASLR remains enabled), a zero linker
timestamp and checkout-prefix mapping; this avoids MinGW's output-path-derived
image base changing relocated addresses/debug metadata. Two fresh local output
directories produced identical final DLL bytes during integration verification.

## Caller-owned resources (never redistributed)

Provide an external immutable directory with all eleven files matching the exact
SHA256 values in `native/resource_hashes.h` (also upstream tools/data.sha256):
msttssyn.dll plus five VCE/CFG pairs (eleven files total). Obtain them under your own license.
The original DLL is read only as binary data, never LoadLibrary-loaded, executed
or emulated by the native build. The resource parser is not a sandbox. Keep the
directory immutable throughout use; validation does not protect against changes
after validation. Use --resources-dir or SAPI4_RESOURCES_DIR for native tests.

No build downloads, vendors or uploads these assets. Absolute UTF-8 drive-local
paths must normalize to ASCII or an existing ASCII short alias and fit the
boundary's 239-byte limit; arbitrary Unicode/UNC paths are not supported.

## Optional private local runtime, not an add-on archive

```
python tools/prepare_runtime.py --dll .local/build01/MSTTSSapi4.dll --addon-output .local/runtime01
```

The default excludes all voice resources. To explicitly copy your own licensed
resources for isolated local tests, add:
`--resources-dir "D:/licensed/sapi4" --include-resources`.
The destination must be new and git-ignored. No archive is produced. The two
tracked modules remain exact 0.5.2; only the **generated local bridge DLL pin** is
changed to the freshly built DLL SHA256. That is an artifact binding change, not
a driver behavior change. Review the DLL and tests before using that runtime.
There is no automatic installation or deployment.

## No-device driver tests

Provide a separate compiled NVDA runtime with library.zip and a compatible
Python 3.13 interpreter (the pinned runtime was verified with Python 3.13.17;
a different OpenSSL/extension build can fail to import even on the same minor):

```
# Set NVDA_RUNTIME in your shell to the external compiled runtime directory.
python tests/driver/test_regression.py --target-root .local/runtime01 --output .local/driver01 --extended
python tests/driver/extra_checks.py --target-root .local/runtime01 --output .local/extra01
python tests/driver/limits.py --target-root .local/runtime01 --output .local/limits01
```

The regression runner also accepts --nvda-runtime instead of NVDA_RUNTIME.
Optional --nvda-source / NVDA_SOURCE records hashes of the external source's
copying.txt and speech manager for provenance, without copying NVDA core.
For all-mode driver smoke, use tests/driver/smoke22.py with --target-root,
--native-report (the native verifier's report.json) and --output.
For a real rejection control, tests/driver/negative_control.py accepts an
external --rejected-root pointing to the preserved 0.5.1 source/runtime plus
--output. It requires the caller-dispatch starvation and cancellation recovery
to reproduce; no rejected driver or synthetic mutation is committed.
The native compare_baseline.py and fragments.py scripts accept --dll,
--resources-dir and --output; compare_baseline.py additionally requires an
external --reference-dll with the exact original private native hash above.

The NVDA bootstrap loads public modules, creates isolated config under ignored
output, and initializes speech state only. It never executes nvda.exe, selects a
live synth, creates wx.App or opens an audio device. The TextInfo endpoint policy
is a fixture, not proof of any application's provider. The source-derived fake
WavePlayer models the 400ms native bound, caller-dispatched callbacks and
registration-after-dispatch order. Generated, fed and independently consumed PCM
hashes/tails, cancellation, silent indexes, FIFO and drain are checked. These
headless checks do not prove physical playback, navigation feel or choppiness.
See LICENSES/NVDA-NOTICE.md for source/provenance and licensing scope.

Native tests include 46 retained goldens with one/four worker threads, 22-mode
smoke, 130 retained original-reference PCM hashes with marker IDs, 1000 same and
mixed lifecycle cycles, stale handles, capacity, deterministic threaded stop,
shutdown/reinitialization and separate partial-setup failure injection. Fixture
files contain authored text/control inputs and expected hashes, not audio or
extracted voice data. Fresh native execution is compared with retained original
reference hashes; these tests do not rerun the historical original engine.

## Ownership and unresolved qualification

Engine and shared allocation domains, recyclable live registry slots, monotonic
opaque handle cookies, tokenizer TLS cleanup and explicit shared shutdown are
retained. Stop/join callers, close handles, end borrowed mode access, require
successful s4_shutdown, exclude new calls, then unload. No teardown in DllMain.
A stale cookie is meaningful only within the same DLL load. Ordinary operations
serialize; stop is thread-safe; callback re-entry into ordinary operations is
rejected. Fragment markers use stock SAPI grammar and do not rewrite punctuation
or introduce transparency/sidecar research.

**Strict whole-process constant-heap qualification remains FAILED/BLOCKED.**
Historical Windows observations include a 3144-byte/six-block OS thread-pool
retirement and a separate unexplained +2272-byte/ten-block increase. Attribution
of one does not explain the other. Zero engine roots, passing lifecycle tests and
module absence are separate evidence, not a leak-free guarantee. This branch
neither relaxes that gate nor claims release approval or new live listening.

Use `tools/audit_staged.py` before committing. Keep local test logs/JSON, objects,
DLLs, PCM/WAV, resources and runtime trees under ignored .local/ only.
