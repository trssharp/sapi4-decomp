# sapi4-decomp: decompilation of Microsoft's SAPI 4 TTS engine (msttssyn.dll) into portable C.
# The vendored x86 emulator (emu/) runs the original DLL and is the oracle; decompiled C in src/ is
# hooked over the original functions and must leave the golden corpus bit-identical.
CC ?= clang
# -ffp-contract=off: no fused multiply-add, so double arithmetic rounds exactly like the emulator's x87
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -fno-builtin -DDECOMP_HOOK
EMU = emu/x86.c emu/emu.c emu/crt.c emu/winapi.c emu/ole.c emu/registry.c emu/sapi4_tts.c
DECOMP = $(wildcard src/*.c)
HOOKS = harness/hooks.c harness/hooktab.c harness/adapters.c
HDR = $(wildcard emu/*.h) $(wildcard src/*.h) harness/hooks.h

all: build/sapi4_speak

build/sapi4_speak: $(EMU) $(DECOMP) $(HOOKS) emu/sapi4_speak.c $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -Iemu -Isrc -Iharness -o $@ $(EMU) $(DECOMP) $(HOOKS) emu/sapi4_speak.c -lm

build/unit: $(DECOMP) $(HOOKS) $(EMU) harness/unit.c $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -Iemu -Isrc -Iharness -o $@ $(EMU) $(DECOMP) $(HOOKS) harness/unit.c -lm

build/sapi4_lockstep: $(EMU) $(DECOMP) $(HOOKS) emu/sapi4_speak.c emu/lockstep.c $(HDR)
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -ffp-contract=off -fno-builtin -DDECOMP_HOOK -DX86_LOCKSTEP -Iemu -Isrc -Iharness -I/opt/homebrew/include -o $@ $(EMU) $(DECOMP) $(HOOKS) emu/sapi4_speak.c emu/lockstep.c -L/opt/homebrew/lib -lunicorn -lm

# oracle checks: reference WAVs from tetyys.com + 46 golden renders, with every hook enabled
check: build/sapi4_speak
	./tools/check_refs.sh
golden: build/sapi4_speak
	./tools/golden.sh
# the same with no hooks (the pure original) - must also pass; proves the harness itself
golden-nohooks: build/sapi4_speak
	SAPI4_HOOKS=none ./tools/golden.sh

clean:
	rm -rf build
.PHONY: all check golden golden-nohooks clean

# coverage build: per-instruction execution counts + call targets of the engine (tools/cover.sh)
build/sapi4_cover: $(EMU) $(DECOMP) $(HOOKS) emu/sapi4_speak.c $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -DX86_COVER -DX86_PROFILE -Iemu -Isrc -Iharness -o $@ $(EMU) $(DECOMP) $(HOOKS) emu/sapi4_speak.c -lm

# every decompiled function against the original in the emulator on random inputs
unit: build/unit
	./build/unit data/msttsl 2000
.PHONY: unit

# every mode x 9 texts (tags, spelling, numbers, long) x 2 warm utterances: hooks vs the original
hookdiff: build/sapi4_speak
	./tools/hookdiff.sh
.PHONY: hookdiff

# src/ must also compile as the portable build (real pointers, no emulator)
portable-check:
	$(CC) -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -fsyntax-only src/*.c
.PHONY: portable-check
