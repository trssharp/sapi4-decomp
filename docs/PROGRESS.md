# Decompilation progress

Target: Microsoft's SAPI 4 text-to-speech engine `msttssyn.dll` ("Speech Build 4.0.4.2512", 1999-01-12,
851,456 bytes, SHA-256 a8a2ddb9...f731), the engine behind Microsoft Sam, Mike, Mary and their 19 voice
modes. Its PDB path is `P:\whistler\ship\msttssyn5\WinRel\msttssyn.pdb` and it names `.\libsrc\cart.cpp`:
this is Microsoft Research's Whistler synthesizer, the ancestor of the SAPI 5 engine (`spttseng.dll`)
that KamiKitsune420/ms-sam-mike-mary-decomp reconstructs (see "Lineage" below).

Verification: every decompiled function is hooked over the original inside the emulator (the original
DLL keeps running around it) and must leave the output bit-identical:

* `make check`: the 5 genuine-engine references from tetyys.com (Sam x2, Mike, Mary, RoboSoft One).
* `make golden`: 46 renders over all 19 modes, two texts, speed/pitch extremes.
* `make unit`: every decompiled function against the original, called directly in the emulator on
  2,000 random inputs each (all output buffers, the return value, x87 results as exact doubles). This
  covers paths the speech corpus never takes (8-bit output, filters of unused modes, odd sizes, ties).

All three pass with every hook enabled. `SAPI4_HOOKS=none` gives the untouched original.

## Where this stands (2026-09-26, first night)

Measured by `tools/progress.py` (static list from `tools/funcs.py`, reachability from `tools/cover.sh`):

| | functions | bytes |
|---|---|---|
| static function list (excl. 73 import stubs; recursive descent + gap seeding, approximate) | 1,000 | ~183,000 of .text 184,302 |
| reached by the golden corpus + tagged text | 523 | 143,316 |
| **decompiled and verified** | **20** (18 reached, 2 unit-tested only) | **3,445** |
| share of the reached set | 3.4% | 2.3% |
| share of all guest instructions executed | | **80.3%** |

The 80% is not a measure of how much is done - it is where the time goes: the inverse FFT alone is
36% of every instruction the engine executes, the all-pole filter 26%, the LSF-to-LPC conversion 9%.
The golden run went from 21 s to 7.4 s because those now run as native code.

## Engine architecture (as understood so far)

Recon from exports, vtables, strings, and a dynamic call graph of the corpus (`tools/cover.sh`):

```
host (SAPI 4 client)
  DllGetClassObject -> class factory -> ITTSEnum (vtable 0x636a0178)
     Next     0x6367eaf3                 enumerate the 19 modes (reads *.cfg / *.vce headers)
     Select   0x6367ef50  (126 functions, 20 KB reached)  loads the voice (.vce: Cdb, Senone, AltUnits,
                                         VcHeader, Inventory, PhoneFile, TreeImage; .cfg: CFG1..7)
  ITTSCentral (vtable 0x6369ff40): TextData 0x6367bcde queues text; Register 0x6367c261; ...
  ITTSAttributes (0x636a0000): PitchSet 0x6367c6ce, SpeedSet 0x6367c7e7, ...
  three worker threads, started around 0x6367b0f9:
    0x636844b6  front end   308 functions, 94 KB reached, 6.7% of instructions:
                text normalisation (abbreviation table, numbers, dates, currency), lexicon and
                letter-to-sound (compressed pronunciations in .rdata), CART trees (cart.cpp),
                phrasing / prosody
    0x63687d3f  unit stage   55 functions, 12.5 KB, 0.5%: senone / unit selection from the voice's
                decision trees (TreeImage, PhoneFile), durations and F0 (sqrt, x87)
    0x636822a1  back end     57 functions, 10.5 KB, 91.4%: the synthesizer - a pitch-synchronous
                LPC vocoder: LSF frames -> LPC (0x63678470), excitation periods from the Inventory
                (0x63675c05 decodes them; inverse FFT 0x63684b60 builds pulses), all-pole synthesis
                filter (0x636712d5), the mode effects (reverb / RoboSoft / whisper: 0x63680189 and
                friends, a C++ object with a delay line), then float -> 16-bit PCM (0x63671ebc) and
                IAudioDest::DataSet through posted window messages
```

Voice data format: `.vce` is an OLE compound file. The `Inventory` stream is byte-for-byte the
synthesizer data the SAPI 5 engine keeps in section 3 of its `.spd` (codebooks, unit blobs; see
ms-sam-mike-mary-decomp `tools/vce2spd.py`).

## Lineage with the SAPI 5 engine (ms-sam-mike-mary-decomp)

Confirmed shared code, not just shared data: the SAPI 5 port's `ifft_split_radix` (`FUN_5ed58259`)
and `lsf_to_lpc` (`FUN_5ed57f9e`) are the same algorithms as msttssyn's 0x63684b60 and 0x63678470,
line for line in structure. Its `sam4fx.c` reconstructs this DLL's voice-mode effects (preset table
at 0x6371f018). Differences that matter for bit-exactness, found while decompiling:

* The SAPI 5 port is "within 1 LSB"; this project must be exact, so the x87 code decides: e.g. the
  FFT parks several intermediates in float stack slots (rounded to float) that the SAPI 5 port keeps
  in double, divides by n instead of multiplying by (float)(1/n), and LSF->LPC uses a double 2*pi
  (6.283185307179586) where the SAPI 5 engine uses the float 6.2831855.

So the SAPI 5 port is an excellent map of *what* each back-end routine does; the exact arithmetic
still has to come from this binary.

## Done (20 functions)

| address | name | what | how verified |
|---|---|---|---|
| 0x636712d5 | dsp_allpole | all-pole synthesis filter (26% of instructions) | golden, check, unit |
| 0x6367131b | dsp_fit_period | fit an excitation period into another length with a window cross-fade | golden, check, unit |
| 0x636713b8 | dsp_period | reverse / copy / fit a period, then gain | golden, check, unit |
| 0x63671943 | dsp_fir | FIR filter in place | golden, check, unit |
| 0x636719a0 | dsp_iir_c0 | recursive filter (never reached by the corpus) | unit |
| 0x63671e49 | dsp_sum_abs | sum of magnitudes (ST0) | golden, check, unit |
| 0x63671e79 | dsp_scale | gain | golden, check, unit |
| 0x63671e96 | dsp_reverse | reversed copy | golden, check, unit |
| 0x63671ebc | dsp_to_pcm16 | float -> 16-bit PCM (fistp, no clipping) | golden, check, unit |
| 0x63671eee | dsp_to_pcm8 | float -> unsigned 8-bit PCM (never reached) | unit |
| 0x63671f31 | dsp_max_abs | peak magnitude (ST0) | golden, check, unit |
| 0x63675c05 | codec_bytes_to_floats | escape-coded byte stream -> floats; returns bytes consumed in EAX | golden, check, unit |
| 0x63678470 | lpc_from_lsf | LSF -> LPC (sorts the LSFs in place when needed) | golden, check, unit |
| 0x63684b60 | fft_inverse_real | Sorensen split-radix inverse real FFT (36%) | golden, check, unit |
| 0x6368864b | dsp_fold_pulse | fold a pulse into a period (overlap-add of its halves) | golden, check, unit |
| 0x636886c6 | dsp_scale_b | a second copy of dsp_scale | golden, check, unit |
| 0x636886e3 | dsp_cmp_float | qsort comparator | unit |
| 0x63688708 | dsp_sort_floats | VC-qsort of floats, returns "was sorted" | golden, check, unit |
| 0x6368874c | dsp_place_halves | place a pulse's halves | golden, check, unit |
| 0x636887a7 | dsp_add_halves | add a pulse's halves | golden, check, unit |

`src/crt_vc.c` also carries the VC runtime's qsort algorithm (the order of equal elements shows in
the output) and `src/x87.h` the x87 conversions (fistp, _ftol, the |x| idiom).

## Pitfalls found so far

* **Implicit return values.** `codec_bytes_to_floats` looks void, but its caller adds EAX (the index
  it stopped at) to its stream pointer. The unit test passed and the golden corpus failed; the fix
  was to return it. `tools/eaxuse.py` now lists, for any function, which call sites read EAX / EDX /
  ST0 after the call - run it before declaring a function void.
* **Float stack slots.** MSVC spills some x87 intermediates to 4-byte stack slots; each is a rounding
  to float that has to be reproduced (`tools/dataflow.py` prints every store with `f32()` marked).
* **Negative control.** Changing one rounding in dsp_allpole (float instead of double subtraction)
  makes `make check` fail with 99.44% of samples identical and max diff 1 LSB - the harness sees
  single-LSB errors.
* The emulator's x87 is IEEE double with 53-bit precision (the Win32 default control word), and its
  CRT math uses the host libm - so native C with doubles and `-ffp-contract=off` matches it exactly.
  (The emulator itself is bit-exact against the genuine engine on the tetyys references.)

## What is left, and the obstacles

1. **The rest of the back end** (39 functions, ~7 KB): the per-frame vocoder driver (0x636719fa),
   pitch-mark / excitation assembly (0x636710da, 0x63671429, 0x636715e2), the effect object
   (0x63680036..0x63680189 and 0x6367fbfe..0x6367ff87: delay lines, allpasses, RoboSoft warble via
   rand()), the synthesis thread loop and audio handoff (0x63681ede, 0x63681b7e, 0x63681cb2), and the
   critical-section queue helpers (0x63684899..0x63684aa1). Most are C++ methods on objects that hold
   pointers; see "Pointers" below.
2. **The unit stage** (55 functions, 12.5 KB): tree walking, durations, F0.
3. **The front end** (308 functions, 94 KB) - the bulk of the work: text normalisation, lexicon,
   letter-to-sound, CART, prosody. Integer / string code, less x87, but big functions (the largest
   reached functions are 2-3.4 KB, e.g. 0x63673312 2,736 bytes, 0x63689fee 2,583 bytes).
4. **Voice loading / COM** (ITTSEnum::Select subtree, 126 functions, 20 KB) and the SAPI 4 COM glue,
   which a portable library would replace rather than reproduce (as OpenTV did).
5. **477 functions the corpus never reaches** (about 42 KB): mostly dialogs, the lexicon manager,
   tags and paths the corpus does not exercise. The corpus needs growing (tags, spelling, user
   lexicon, long sentences, the 8 kHz modes are already in) before those can be verified by
   anything but the unit tester.

**Pointers.** The emulator is 32-bit guest on a 64-bit host, so C structs holding pointers cannot be
shared between guest and native code the way OpenTV's 32-bit hook build shares them. Leaf functions
take guest pointers translated at the adapter (done). For objects with pointer fields the plan is a
field type that is a 32-bit guest address in the hook build and a real pointer in the portable build
(`GPTR(T)` with get/set helpers), so the same source compiles both ways.

**Threads.** The engine runs three worker threads with events and critical sections. The portable
library will want a single-threaded pipeline (as OpenTV's did); the hook build keeps the original
threads, so thread-structure changes can only be verified at the end, by the portable build against
the golden hashes.

## Estimate

Honest, from tonight's rate: leaf DSP functions took 5-20 minutes each including tests; the FFT (1.4
KB of dense x87) about 40 minutes with the dataflow tool. The remaining reached code is ~140 KB, a
large part of it front-end string handling that decompiles faster per byte than x87 but has far more
control flow. OpenTV took 223 functions / 151 KB for a comparable engine. A realistic figure for this
one is **4-8 weeks of focused sessions** to a portable build that reproduces the golden corpus, with
the back end (weeks 1) the quickest part and the front end (weeks 2-6) the bulk; the unreached 42 KB
is extra and depends on growing the corpus.
