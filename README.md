# sapi4-decomp

A decompilation of Microsoft's SAPI 4 text-to-speech engine `msttssyn.dll` (1999: Microsoft Sam, Mike,
Mary and their 19 voice modes) into portable C, verified byte for byte against the original, in the
manner of OpenTV (github.com/RetroBunn/tv-decomp). Work in progress: see docs/PROGRESS.md.

No Microsoft binaries or voice data are in this repository, and none may be added (`.gitignore`).

## Bring your own engine

This repository is code only. To build or run anything here you need your own copy of Microsoft's
SAPI 4 engine from a SAPI 4 install: `msttssyn.dll` (Speech Build 4.0.4.2512, 851,456 bytes, SHA-256
`a8a2ddb9b82b4325e1315f481302cd5ba9edcf3742425ed582370f1b3940f731`) and its voice files (`*.vce`,
`*.cfg`). The portable build reads the engine's tables from that DLL at run time. A build that bakes the
DLL's data or the voices in (`make port-render-full-check`, `make api-baked-check`) contains Microsoft's
data, so keep it to yourself rather than redistributing it.

## Setup

Put `msttssyn.dll` with its voice files (`*.vce`, `*.cfg`) in `data/msttsl/` (a symlink is fine) and the
tetyys.com reference WAVs in `samples/ref/`. Python 3 with `capstone` and `pefile` for the tools.

## How it works

`emu/` is a pure-C x86 interpreter plus a small Win32/COM shim (vendored from the Microsoft Classic
Voices SAPI 4 work) that runs the original DLL bit-identically. `src/` is the decompiled C: portable,
no emulator types. `harness/` hooks each decompiled function over the original (the entry gets the
host-call trap `F1 lo hi`; `harness/adapters.c` translates guest arguments and results) so the rest of
the original keeps running around it.

```
make                 # build/sapi4_speak, with every hook in harness/hooklist.h enabled
make check           # tetyys.com references, bit-identical
make golden          # 46 renders over all 19 modes against tools/golden_hashes.txt
make unit            # each decompiled function vs the original on random inputs (heap too)
make hookdiff        # 171 renders (19 modes x 9 texts x 2 warm utterances), hooks vs the original
make portable-check  # src/ type-checks as the portable build (no emulator, real pointers)
make port-lex-check  # the portable build's first piece (port/): the main lexicon as C alone, vs the original
SAPI4_HOOKS=none|all|a,b|-a,-b  build/sapi4_speak ...     # choose hooks (bisecting)
SAPI4_HOOK_STATS=1   build/sapi4_speak ...                # how often each hook ran
```

## Tools

| | |
|---|---|
| `tools/funcs.py DLL work` | recursive-descent function list -> work/functions.{json,txt} |
| `tools/cover.sh` | coverage + dynamic call graph of the original over the corpus -> work/coverage.*, work/callgraph.* |
| `tools/listing.py DLL work ADDR...` | annotated disassembly of functions |
| `tools/dataflow.py DLL FROM TO` | symbolic x87/x86 execution of a straight-line run: every store as an expression with the float roundings marked |
| `tools/eaxuse.py DLL work ADDR...` | which callers read EAX / EDX / ST0 after a call (implicit return values) |
| `tools/dupes.py DLL work` / `tools/instances.py DLL work ADDR...` | groups of same-code functions (template copies) and each copy's globals |
| `tools/families.py DLL work` | near-duplicate functions by mnemonic sequence (template copies with edits) |
| `tools/hookdiff.sh` | all modes x 9 texts, hooks vs the original: PCM and final guest heap |
| `tools/progress.py work` | the numbers in docs/PROGRESS.md |

Hooks: `HOOK(addr, name, argbytes)` in harness/hooklist.h runs the C directly; `HOOKY` runs it on a
coroutine so it can Sleep or block mid-function like the original. Decompiled C that needs a function
not decompiled yet calls it through harness/guest.c (`make portable-check` lists those). The C runtime
semantics shared by the emulator and the decompiled code live in src/vcrt.c.

`port/` is the portable build as it grows: `dllimage.c` loads the DLL's sections from the user's copy at
run time (the tables stay Microsoft's, read where they are; relocated dwords become host pointers), so
`DLLVAR` / `DLLPTR` in src/ work without the emulator.

Addresses everywhere are the DLL's own (preferred base 0x63670000).

## Licence

The code in this repository is MIT licensed (see `LICENSE`). That covers the code written here only:
it grants no rights to Microsoft's engine, its data or its voices. Not affiliated with or endorsed by
Microsoft.
