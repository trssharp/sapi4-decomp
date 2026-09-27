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

## Where this stands (session 4, 2026-09-27)

Measured by `tools/progress.py` (static list from `tools/funcs.py`, reachability from `tools/cover.sh`):

| | functions | bytes |
|---|---|---|
| static function list (excl. 73 import stubs; approximate) | ~1,000 | ~185,000 of .text 184,302 (boundaries approximate) |
| reached by the corpus | 536 | 145,161 |
| **decompiled and verified** | **299** (267 reached, 32 reached only by the unit tests) | **75,438** |
| share of the reached set | 49.8% | 50.0% |
| share of all guest instructions executed | | 98.5% |

Session 4 added 129 hooked functions (44.5 KB): the rest of the rule machine (rule execution, rule
match, the rule-set main loop, rule apply: 3 instances each), every rule procedure the three machines
call (87 table entries: 60 wrappers that run another rule set, 27 with work of their own), the
lexicon (the ten word dictionaries and the Huffman-coded trie of the main lexicon), the word module's
duration increments, and the first piece of the portable build: `make port-lex-check` runs the main
lexicon as C alone on 27,100 words, identical to the original (see "Done, session 4").

Session 3 added 59 (12.7 KB): the thread plumbing of the unit stage and the synthesizer, most of the
alternative-unit search, and the rule machine's tape routines, condition interpreter and runner.

History of the earlier sessions:

Session 1 did the synthesizer's signal path (38 functions). Session 2 added 73:
* **Unit stage** (the thread between front end and synthesizer): SAPI tag lexer and parser (11),
  its containers (hash table, vectors: 11), the senone decision-tree lookup and phone fix-ups,
  per-phrase unit selection, the prosody driver and both contour-interpolation passes (b[] and c[]).
  What is left of it: the alternative-unit lattice search (0x63686614 / 0x63685c20 / 0x636882fc /
  0x63685d72, mostly unreached), one notification timing routine (0x63687e26) and the thread
  plumbing (queues, events, PostMessage), which is now hookable (see "Coroutine hooks").
* **The C-runtime equivalence layer**: `src/vcrt.c` holds MSVCRT's C-locale ctype, the sign-only
  string/memory compares, stricmp/strnicmp, CP1252 and USER32's character functions, lstrcmp(i)A's
  word sort and MultiByteToWideChar. The emulator's own shims (`emu/crt.c`, `emu/winapi.c`) now call
  these, so there is exactly one definition; the emulator stayed bit-exact on every check.
  `src/crt_vc.c` adds the VC qsort, rand, and malloc/realloc/free/Sleep, which the hook build routes
  to the guest heap / guest thread state.
* **Front end, from its leaves**: 21 small helpers (text span accessors etc.), and the first templates
  of its generated rule modules: the DLL has ~10 modules compiled from the same templates, so
  `tools/dupes.py` groups identical code and `tools/instances.py` lists each copy's globals; one C
  function then serves every copy (tape helpers x19, table binary search x5).

Verification, all with every hook enabled: `make check` 5/5, `make golden` 46/46, `make hookdiff`
171/171 renders identical to the original in PCM **and in the final guest heap state**, `make unit`
173k checks over 1,000-2,000 random cases per function (struct methods run original and C on the
same guest memory; allocating functions also compare the whole heap; negative controls done for the
new areas).

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

## Done, session 1: the signal path (38 functions)

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

## Done, session 2 (73 functions)

| address(es) | name(s) | what | verified by |
|---|---|---|---|
| 0x63680f65, 0x63681002, 0x6368104a, 0x636810a6, 0x636810de, 0x6368114d | tag_lex, tag_lex_number/word/string/brace, tag_keyword | SAPI tag lexer | golden, hookdiff (tags), unit (token stream + buffers) |
| 0x63681196, 0x6368119e, 0x636811d9, 0x63681237, 0x63681315 | tag_parse_*, tag_parse | tag parser (22-keyword table in .data) | golden, hookdiff, unit (with heap) |
| 0x63675e95, 0x6367fb1e, 0x6367fb43, 0x63685bb9 | hash_lookup, named_index, named_item, prefix_cmp | string hash table, name lookups | golden, hookdiff |
| 0x63688151..0x6368824c, 0x63685814 | intvec_*, vecarray_*, blob_copy80 | vectors, blobs | golden, hookdiff (heap) |
| 0x63674184 | senone_lookup | senone decision trees | golden, unit (random trees) |
| 0x636876b2 | phone_fixups | DX->T, unstressed diphthongs stressed | golden, unit |
| 0x63687751 | unit_select | phone index + senone per phone of a phrase | golden, hookdiff |
| 0x636871a4 | unit_prosody | durations, state values, output units | golden, hookdiff (heap) |
| 0x63686c7d | unit_prosody_a | b[] contour interpolation, state insertion | golden, unit (random phrases) |
| 0x63686905 | unit_prosody_b | c[] contour interpolation | golden, unit (random phrases, normalised by the original A) |
| 0x6369d3a0..0x6369d5a0 etc. (21) | span_*, pair_reset, block8_clear, free_if, is_digit_char, char_class16, freelist_push, ... | front-end helpers | golden, hookdiff |
| 0x63697558 + 8 copies | tapes_graphic_0..8 | rule modules: tape range all graphic | golden, unit (every copy) |
| 0x63697583 + 9 instances | tapes_pad_0..9 | rule modules: pad the four tapes | golden, unit (every instance) |
| 0x63697b42 + 4 instances | rule_bsearch_0..4 | rule modules: binary search in the module's table | golden, unit (every instance, real tables) |

`src/crt_vc.c` also carries the VC runtime's qsort algorithm (the order of equal elements shows in
the output) and `src/x87.h` the x87 conversions (fistp, _ftol, the |x| idiom).

## Done, session 3 (59 functions)

| address(es) | name(s) | what | verified by |
|---|---|---|---|
| (IAT thunks) | harness/win32.c | EnterCriticalSection / WaitForMultipleObjects park the coroutine and retry; LeaveCS, SetEvent, ResetEvent, PostMessageA, SetThreadPriority call the emulator's own thunks; the wait logic is shared with the emulator (`emu_wait_poll`, `emu_cs_try_enter`) | hookdiff (heap) |
| 0x63684899, 0x63684905, 0x6368498f, 0x636849ab, 0x636849cd, 0x63684a29, 0x63684aa1, 0x6368447a, 0x6368225f, 0x63687cfd | queue_* (src/queue.c) | stage message queue: growth, high/low-water events, push/pop, locked getters | golden, hookdiff (heap) |
| 0x63687e26, 0x63687d4b, 0x63687de5, 0x63687fbb | unit_rate_update, unit_set_rate, unit_reset_rate, unit_set_amp | duration scale, final silences, notifications | golden, hookdiff, unit |
| 0x63687d3f, 0x63687ba4, 0x63687974, 0x6368571f | unit_thread_proc, unit_thread_loop, unit_phrase, unit_send | unit-stage thread, phrase driver, hand-off to the synthesizer (HOOKY) | golden, hookdiff (heap) |
| 0x63686614, 0x63685c20, 0x63685d18, 0x636882fc, 0x63685d00, 0x63685bf7, 0x636881b0, 0x636882d6 | unit_lattice, lattice_match, unit_span_lookup, lattice_viterbi, lattice_cost, array_lacks, intvec_append, vecarray_append | alternative-unit search: rule matching, span lookup, Viterbi over the lattice | golden, hookdiff, unit (synthetic lattices with a competing cost function, heap snapshots) |
| 0x636822a1, 0x63681ede, 0x63681cb2, 0x63681b7e, 0x63674d18, 0x6367ddfb | voc_thread_proc, voc_thread_loop, voc_synth_request, voc_send_audio, phone_table_index, identity32 | synthesis thread, request loop (unit to float samples, growth, clipping), audio hand-off with flow control | golden, check, hookdiff |
| 3 instances each (A 0x6368.., B 0x6369.., C 0x6369..; addresses in src/fe_vm.c) | vm_shift_right/left, vm_pop_range, vm_push_until, vm_skip_block, vm_move, vm_cond, vm_run, vm_try | the front end's rule machine: tape shifts, stack moves, bytecode block skip, condition interpreter, the rule runner with backtracking over choice points, trying one rule (27 hooks, one C function per routine) | golden, hookdiff, unit (random bytecode per instance; the original is aborted-safe) |

`tools/families.py` finds near-duplicate functions (same mnemonic sequence, different globals or
small edits) - that is how the three rule-machine instances were matched; instance A differs in
reading one position as int16 and in its apply guard.

## Done, session 4 (129 functions)

| address(es) | name(s) | what | verified by |
|---|---|---|---|
| 0x6368d966, 0x63691990, 0x63694ed7 | vm_match | the rule pattern matcher: literals, classes, variables, tape switches, bounded repetitions, embedded conditions | golden, hookdiff, unit (random patterns over small alphabets) |
| 0x6368f187, 0x636931b1, 0x636966f8 | vm_apply | a rule's output: a procedure (0xfe,n) or a replacement string, keeping per-character counts | golden, hookdiff, unit |
| 0x6368e53d, 0x63692568, 0x63695aaf | vm_exec | one context opcode: tests, filler skips, alternatives and optional parts (choice points), repetitions | golden, hookdiff, unit (every opcode on random state) |
| 0x6368c997, 0x636909b7, 0x63693ef8 | vm_main | a rule set over a segment: 16 scan strategies (forward / backward, anchored or not, all or first, single rules or groups); its frame lives on the guest stack (`vc_stack_push`) because procedures of the original get pointers into it | golden, hookdiff, unit (real rule sets on random segments; the rule types the data never uses, 9-12 and 15, by patching types in the emulator's .rdata) |
| 60 addresses (fe_procs.c table) | vm_proc (PROC_MAIN) | the rule procedures that run another rule set | unit (.data, tapes and heap compared) |
| 0x6368b28b, b2e0, b305, b383 (+ b468, b526), b6bc; 0x6369048f (+ 0x636905f9), 0x63690633, 0x63690215, 0x6369024b; 0x636939b0, 3984, 38b9, 38ef, 3925, 382c, 3a03, 397f | vm_proc (tape procedures) | prosody-tag numbers into the word object, quotes, dash numbering, pause placement, class marks, "x(y)" alternatives | golden, hookdiff, unit (tapes drawn from the symbols each one tests) |
| 0x6369a9c8, 0x6369a87e + 7, 0x6369a47b | lex_pack, lex_field, lex_field18 | letter-code key packing, packed n-bit field arrays | unit |
| 0x63698968, 0x63699322, 0x63698e44, 0x63697675, 0x63697e71 | lex_bsearch8w, lex_bsearch16, tapes_printable | the dictionaries' other searches | golden, hookdiff |
| 0x6369a4ea, 0x6369773d, 0x636980bf, 0x63698a3e, 0x63698594, 0x63698f1a, 0x636993f8, 0x63697c17, 0x636972c7 | lex_lookup | nine dictionaries, one function, the variants as data (`lex_dicts`) | golden, hookdiff, unit (half the words unpacked from the dictionaries' own keys) |
| 0x6369986b, 0x63699bd5, 0x6369a15f, 0x63699cd6, 0x63699d94, 0x63699e19, 0x63699e9e, 0x63699f23, 0x63699fae, 0x6369a0c9 | lex_main, lex_trie_* , lex_huff_* | the main lexicon: Huffman-coded trie walk, entry number, pronunciation with back-references | golden, hookdiff, unit (15,418 words of /usr/share/dict/words the lexicon has), port-lex-check |
| 0x63696dd8, 0x63696f5f, 0x6369713a, 0x63697261 | word_durations, word_cluster, word_cluster_find, word_stop_weight | letter-cluster codes and per-position duration increments of the word object | golden, hookdiff, unit |

Negative controls were run for each group (one changed comparison or constant per run); where a
change was not detected it was confirmed to be unobservable (e.g. an equality the data never
produces) or a generator was sharpened until it was.

## The front end, mapped (sessions 3 and 4)

Session 3 inferred roles from call counts on probe texts; session 4 read the code, which corrects one
of them: the "nonword-only driver" at 0x63698f1a.. is two of the dictionaries below (morpheme / affix
lists consulted when the main lexicon has no entry), not a separate letter-to-sound driver.

| part | role | status |
|---|---|---|
| rule machine A (0x6368c997..) | text normalisation rules (numbers, abbreviations, prosody tags) | machine done; its 14 procedures done; the per-word driver around it (0x6368c49b, 0x6368f732, 0x6368bab2, ...: ~9 KB) not yet |
| rule machine B (0x636909b7..) | post-lexical and phrasing rules (pauses, word classes) | done, with its 14 procedures |
| rule machine C (0x63693ef8..) | letter-to-sound and dictionary lookup: its procedures call the dictionaries | done, with its 59 procedures |
| main lexicon (0x6369986b, trie 0x63699bd5..0x6369a15f) | a letter trie coded as Huffman fields in a bit stream (0x636fdbb8, 111 KB), entry -> 18-bit offset (0x63722090) into a digram-coded pronunciation table (0x636d4340, 164 KB), with back-references between entries | done |
| ten dictionaries (0x6369a4ea, 0x6369773d, ..., 0x636972c7) | sorted packed-key word lists by length and first letter (5- or 6-bit letter codes), each entry -> an n-bit offset into a digram-coded pronunciation; up to three fields ('!', '#' or '"' separated) written to tapes A, C, D | done: one C function, the variants described by data (row layout, search width, field order) |
| word modules 0x63696cd4.. and 0x6369aaa4 (5.4 KB), 0x6369c9e6 (1.6 KB) | the word loop, syllables / stress, phrase assembly | not yet |

Rule bytecode and the lexicon tables are data (A about 3.4 KB at 0x636a2519, B 7.6 KB at 0x636a5ea1,
C 29 KB at 0x636af2a1; lexicon 164 + 111 + 71 KB, dictionaries 20-60 KB each); the portable build will
load them from the DLL, like the voice data.

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
* **Heap identity catches what audio does not.** Comparing the guest heap after a render with and
  without hooks found a macro that evaluated `realloc` twice (GPSET, fixed) - the PCM was identical.
  `tools/hookdiff.sh` now requires identical heaps too.
* **Yields must happen where the original yields.** Heap identity then showed that a hooked
  function that calls Sleep(0) changes the thread interleaving if the yield is deferred to its
  return. Hooks marked `HOOKY` run their C on a per-guest-thread coroutine (ucontext) that is parked
  at the Sleep and resumed when the guest thread is scheduled again (the trap instruction simply
  re-executes). The same mechanism can block (EnterCriticalSection, waits), which makes the thread
  plumbing hookable after all.
* **32-bit address arithmetic.** `DLLVAR` added the load delta to a host pointer instead of to the
  32-bit guest address; it happened to work where the result was converted back to a guest address,
  and read garbage (ASLR-dependent) elsewhere. Fixed; every guest address sum is now done in uint32.
* **x87 branch conditions are NaN-exact** (`x87_je`/`x87_jb`/... in `src/x87.h`): an unordered compare
  reads as "equal" and "below" at the same time, which the C translation must reproduce.
* **Sub-integer differences can be invisible to the corpus.** The prosody contours are rounded to
  int16 before they reach the synthesizer: a deliberate 1e-4 error there left the golden corpus
  identical. Those functions are verified by unit tests on their raw output (which catch it).
* **The original can hang on inputs it never gets.** Contour pass B loops forever on phrases pass A has
  not normalised; its unit test first runs the original pass A (as the engine does).
* **Functions under 3 bytes cannot be hooked** (the trap is 3 bytes); three 1-byte `ret` functions in
  the front end are left out of the counts.
* **Unreachable-in-practice inputs** where C and original must differ are marked: a tag starting with
  "=" makes the original call address 1 (C returns E_FAIL); a > 1023-character tag and a
  delta frame with no preceding full frame read stale stack in the original (C reads zeros). The hook
  build prints `decomp WARNING` if any of these ever happens; none has in any render.

* **Blocking in a hook.** A decompiled thread loop that waits must park where the original waits;
  `vc_WaitForMultipleObjects` / `vc_EnterCriticalSection` poll with the emulator's own wait logic and
  park the coroutine until they succeed. Called outside a `HOOKY` hook they fault, on purpose.
* **Originals that read past their arrays.** `vecarray_append` accepts an index equal to the count
  and reads one element past the end; random tests stay inside what the engine can produce.
* **Random bytecode can hang the original.** Unit tests of the rule machine generate structured
  bytecode (every block closed, filler after each closer) and abort if the original faults, since a
  faulted emulator CPU leaves the next comparison meaningless.

## What is left, and the obstacles

Reached and not yet decompiled: 269 functions, ~72.6 KB. By area:

1. **Front end outside the rule machines and the lexicon (~40 KB):** the front-end thread and text
   input (0x63684233, 0x63689357, 0x63689726, 0x63689fee ...: ~9 KB), machine A's per-phrase driver
   (0x6368c49b with 0x6368bab2, 0x6368f732, 0x6368fed5, 0x6368c6f1 ...: ~8 KB of duration / accent
   heuristics over the word object), the phrase prosody at 0x636827b0..0x6368337f and 0x636837a4 /
   0x6368043f (~10 KB), and 0x6369aaa4 (5.4 KB) with 0x6369c9e6 (1.6 KB).
2. **Voice loading / COM** (~29 KB reached in 0x63671000..0x63680000): to be replaced rather than
   reproduced in the portable build.
3. **Unit stage leftovers**: the lattice phrase rewrite 0x63685d72 (2,210 B, barely reached), the tree
   query 0x6367233c (never reached).
4. **Synthesizer leftovers**: its init 0x636819e6 (C++ exception frame) and the resamplers.
5. **~460 functions the corpus never reaches** (~40 KB).
6. **The portable build**: `port/` loads the DLL's data at run time and runs the main lexicon;
   a driver for the whole pipeline needs the rest of the front end and a single-threaded replacement
   for the thread plumbing.

**Pointers.** The emulator is 32-bit guest on a 64-bit host, so C structs holding pointers cannot be
shared between guest and native code the way OpenTV's 32-bit hook build shares them. Leaf functions
take guest pointers translated at the adapter. Objects with pointer fields use `GPTR(T)` (src/gptr.h):
a 32-bit guest address in the hook build and a real pointer in the portable build, so the same source
compiles both ways (`clang -fsyntax-only src/*.c` without DECOMP_HOOK passes). Globals that hold
pointers are accessed with `DLLPTR(T, addr)`: a 32-bit slot in the hook build, a host-pointer slot in
the portable build (port/dllimage.c, filled from the DLL's relocations).

**Threads.** The engine runs three worker threads with events and critical sections. The portable
library will want a single-threaded pipeline (as OpenTV's did); the hook build keeps the original
threads, so thread-structure changes can only be verified at the end, by the portable build against
the golden hashes.

## Estimate

Session 4 added 129 functions / 44.5 KB - three times session 3 - because the rule machine's
instances share code, its procedures are mostly one template, and the dictionaries are one function
over data. Half of the reached code (50.0% of bytes) is now C, and 98.5% of executed instructions. The
risk named last time (the large rule-machine routines) is gone. What remains of the front end is
hand-written heuristics over the word object (~40 KB, no templates to share), plus voice loading to
replace. Revised estimate: **3-5 more weeks** of focused sessions to a portable build that reproduces
the golden corpus.
