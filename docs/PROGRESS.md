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

## Where this stands (2026-09-26/27, first session)

Measured by `tools/progress.py` (static list from `tools/funcs.py`, reachability from `tools/cover.sh`):

| | functions | bytes |
|---|---|---|
| static function list (excl. 73 import stubs; recursive descent + gap seeding, approximate) | ~1,000 | ~183,000 of .text 184,302 |
| reached by the golden corpus + tagged text | 523 | 143,316 |
| **decompiled and verified** | **38** (35 reached, 3 reached only by the unit tests) | **7,908** |
| share of the reached set | 6.7% | 5.4% |
| share of all guest instructions executed | | **89.6%** |

The 90% is where the time goes, not how much is done: the inverse FFT alone is 36% of every
instruction the engine executes, the all-pole filter 26%, LSF-to-LPC 9%. The golden run went from
21 s to 3.6 s because those now run as native code.

**The synthesizer's signal path is complete**: for every unit the engine synthesizes, everything from
the voice's Inventory bytes to the float samples handed to the audio layer is decompiled C -
`voc_synth_unit` and everything below it (unit decoding, LSF->LPC, codebook spectra, inverse FFT,
pitch marks, excitation assembly, the LPC synthesis filter, the FIR / IIR mode filters and the
Hall / Stadium / Space / RoboSoft effect chain). What remains of the back-end thread is plumbing: its
queues, events, the thread loop and the hand-off to IAudioDest (see "What is left").

Verification of all of it, each run with every hook enabled:
* `make check`: 5/5 tetyys.com references bit-identical.
* `make golden`: 46/46.
* `make hookdiff` (tools/hookdiff.sh): all 19 modes x 9 texts (tags, spelling, numbers, abbreviations,
  a long sentence) x 2 utterances on a warm engine = 171 renders, identical to the original
  (`SAPI4_HOOKS=none`) - an oracle that needs no recorded hashes.
* `make unit`: 96,045 checks over 2,000 random cases per function, 0 mismatches. Struct methods run the
  original and the C on the same guest memory (regions restored in between); allocating functions
  also compare the whole guest heap (memory, free lists, top) - a leaked buffer is caught (negative
  control done).
* The guest heap after a render is identical with and without hooks (`SAPI4_HOOK_STATS=1` prints it),
  so the decompiled allocations happen in the original's order and sizes.
* Nested hooks were also verified separately: `SAPI4_HOOKS=-voc_synth_unit,...` lets the original
  driver call the decompiled pieces individually (golden identical).

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

## Done (38 functions)

| address | name | what | verified by |
|---|---|---|---|
| 0x636710da | voc_pitch_marks | pitch-mark placement from the pitch contour (interpolated), unvoiced stretches, reversed repeats | golden, check, hookdiff |
| 0x636712d5 | dsp_allpole | all-pole LPC synthesis filter (26% of instructions) | golden, check, unit |
| 0x6367131b | dsp_fit_period | fit an excitation period to a new length with a window cross-fade | golden, check, unit |
| 0x636713b8 | dsp_period | reverse / copy / fit a period, then gain | golden, check, unit |
| 0x63671429 | voc_excitation | assemble the excitation, one period per pitch mark, gain contour | golden, check, hookdiff |
| 0x636715e2 | voc_synth_unit | synthesize one unit request (the per-unit driver) | golden, check, hookdiff |
| 0x63671943 | dsp_fir | FIR filter in place (mode filter) | golden, check, unit |
| 0x636719a0 | dsp_iir_c0 | recursive mode filter (not reached by the corpus) | unit |
| 0x636719fa | voc_unit_decode | decode a unit: periods, LSF frames -> LPC, gains, excitation via codebook spectra + inverse FFT, or noise | golden, check, hookdiff |
| 0x63671e49 | dsp_sum_abs | sum of magnitudes (ST0) | golden, check, unit |
| 0x63671e79 | dsp_scale | gain | golden, check, unit |
| 0x63671e96 | dsp_reverse | reversed copy | golden, check, unit |
| 0x63671ebc | dsp_to_pcm16 | float -> 16-bit PCM (fistp, no clipping) | golden, check, unit |
| 0x63671eee | dsp_to_pcm8 | float -> unsigned 8-bit PCM (not reached) | unit |
| 0x63671f31 | dsp_max_abs | peak magnitude (ST0) | golden, check, unit |
| 0x63675bc4 | voc_unit_free | free a unit | golden, heap identical |
| 0x63675c05 | codec_bytes_to_floats | escape-coded byte stream -> floats; returns bytes consumed in EAX | golden, check, unit |
| 0x63678470 | lpc_from_lsf | LSF -> LPC (sorts the LSFs in place when needed) | golden, check, unit |
| 0x63684b60 | fft_inverse_real | Sorensen split-radix inverse real FFT (36%) | golden, check, unit |
| 0x6367fbfe | effect_db_to_gain | dB -> linear (ST0) | golden, unit |
| 0x6367fc36 | effect_ap_clear | clear a delay line | golden |
| 0x6367fc54 | effect_ap_init | set up an allpass stage | golden, heap |
| 0x6367fcb7 | effect_build_stages | allocate the preset's stages | golden, heap |
| 0x6367fd50 | effect_free | free stages and scratch (not reached) | unit (with heap snapshot) |
| 0x6367fda2 | effect_preset | mode 1..7 -> preset table in .data | golden, unit |
| 0x6367fdf5 | effect_init | set up the effect for a mode and rate | golden, unit (with heap snapshot) |
| 0x6367fee8 | effect_ctor | constructor | golden, unit |
| 0x6367ff1d | effect_gain_copy | send copy | golden, unit |
| 0x6367ff87 | effect_mix_clip | dry + wet, clipped to 16 bits | golden, unit |
| 0x63680036 | effect_allpass | one allpass delay line (optional FIR in the loop) | golden, unit |
| 0x63680155 | effect_chain | up to five allpasses | golden, unit |
| 0x63680189 | effect_process | the effect over a buffer, RoboSoft warble from rand() | golden, unit |
| 0x6368864b | dsp_fold_pulse | fold a pulse into a period (overlap-add of its halves) | golden, check, unit |
| 0x636886c6 | dsp_scale_b | a second copy of dsp_scale | golden, check, unit |
| 0x636886e3 | dsp_cmp_float | qsort comparator | unit |
| 0x63688708 | dsp_sort_floats | VC-qsort of floats, returns "was sorted" | golden, check, unit |
| 0x6368874c | dsp_place_halves | place a codebook entry's halves into the spectrum | golden, check, unit |
| 0x636887a7 | dsp_add_halves | add them (delta frames) | golden, check, unit |

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
* **The compiler rewrites libm calls.** clang turned `pow(10.0, x)` into `exp10(x)`, 1 ulp off in some
  cases (the unit test caught it at -8 dB). All code builds with `-fno-builtin`.
* **Sleep(0) and rand() have state.** `Sleep` advances the emulator's virtual clock and yields; `rand()`
  is per guest thread. The hook build routes `vc_sleep` / `vc_rand` / `vc_malloc` to the emulator's
  own implementations (`harness/hooks.c`), the portable build to trivial ones (`src/crt_vc.c`).
* **Pointers in structs.** `src/gptr.h`: a field is `GPTR(T)` - a 32-bit guest address in the hook
  build (so layouts are the original's, statically asserted with `LAYOUT`) and a real pointer in the
  portable build; code converts with `GP()` / `GPSET()`. DLL globals are `DLLVAR(T, addr)`.
* **Uninitialised stack.** `voc_unit_decode` could read a spectrum left on the stack by an earlier
  call if a delta-coded frame came before any full frame; the C zero-fills instead and the hook build
  warns (`decomp WARNING`) if that ever happens. It never has in any render so far.
* The emulator's x87 is IEEE double with 53-bit precision (the Win32 default control word), and its
  CRT math uses the host libm - so native C with doubles and `-ffp-contract=off` matches it exactly.
  (The emulator itself is bit-exact against the genuine engine on the tetyys references.)

## What is left, and the obstacles

1. **The back end's plumbing** (~15 functions, ~3 KB): the synthesis thread (0x636822a1 -> 0x63681ede),
   its request loop (0x63681cb2: grows buffers, calls voc_synth_unit), the audio hand-off
   (0x63681b7e: float -> PCM, PostMessage to the audio window, flow control), set-up (0x636819e6, C++
   with an exception frame), and a thread-safe queue with high/low-water events (0x63684899..
   0x63684aa1, 0x6368225f). These call EnterCriticalSection / SetEvent / WaitForSingleObject /
   PostMessageA, which a hook cannot call halfway (a host function cannot block), so they are not
   hookable one by one; the portable build replaces them with a single-threaded pipeline, as OpenTV
   replaced its SAPI engine thread. Verification for that layer is end-to-end only (portable build vs
   golden hashes).
2. **The unit stage** (thread 0x63687d3f, 55 functions, 12.5 KB): decision-tree walking (TreeImage,
   PhoneFile), durations and F0. Mostly integer and string code plus some x87; hookable.
3. **The front end** (thread 0x636844b6, 308 functions, 94 KB) - the bulk of the work: text
   normalisation, lexicon, letter-to-sound, CART, prosody. Big functions (0x63673312 is 2,736 bytes,
   0x63689fee 2,583). Needs a C runtime layer that reproduces the emulator's MSVCRT exactly (string
   compare returning only the sign, the C-locale ctype table, strtok per thread, sprintf/sscanf);
   the plan is to move those out of emu/crt.c into src/crt_vc.c and have the emulator call them, so
   there is one definition.
4. **Voice loading / COM** (ITTSEnum::Select subtree, 126 functions, 20 KB) and the SAPI 4 COM glue,
   which a portable library would replace rather than reproduce.
5. **477 functions the corpus never reaches** (~42 KB): dialogs, the lexicon manager, tags and paths
   the corpus does not exercise. The corpus needs growing (tags, spelling, user lexicon) before those
   can be verified by anything but the unit tester.
6. **The data.** The portable build needs the DLL's tables (.rdata 515 KB, .data 120 KB: abbreviation
   table, lexicon, effect presets, ...). OpenTV's answer applies: an extraction tool that regenerates
   them from the user's own DLL at build time, never committed. `DLLVAR()` marks every use.

**Pointers.** The emulator is 32-bit guest on a 64-bit host, so C structs holding pointers cannot be
shared between guest and native code the way OpenTV's 32-bit hook build shares them. Leaf functions
take guest pointers translated at the adapter. Objects with pointer fields use `GPTR(T)` (src/gptr.h):
a 32-bit guest address in the hook build and a real pointer in the portable build, so the same source
compiles both ways (`clang -fsyntax-only src/*.c` without DECOMP_HOOK passes; there is no portable
driver yet, so nothing links it).

**Threads.** The engine runs three worker threads with events and critical sections. The portable
library will want a single-threaded pipeline (as OpenTV's did); the hook build keeps the original
threads, so thread-structure changes can only be verified at the end, by the portable build against
the golden hashes.

## Estimate

From this session's rate: 38 functions / 7.9 KB, including all the x87-heavy DSP, in one session,
with the tooling built along the way (the dataflow tool made the FFT and the pitch marks tractable).
Integer/string front-end code decompiles faster per byte but has far more control flow and needs the
CRT layer first. The remaining reached code is ~135 KB. OpenTV's comparable engine was 223 functions /
151 KB. A realistic figure is **5-8 more weeks of focused sessions** to a portable build that
reproduces the golden corpus: unit stage ~1 week, front end 3-5 weeks, voice loading + data extraction
+ portable driver ~1-2 weeks. The unreached 42 KB is extra and depends on growing the corpus.
