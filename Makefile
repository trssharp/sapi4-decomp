# sapi4-decomp: decompilation of Microsoft's SAPI 4 TTS engine (msttssyn.dll) into portable C.
# The vendored x86 emulator (emu/) runs the original DLL and is the oracle; decompiled C in src/ is
# hooked over the original functions and must leave the golden corpus bit-identical.
CC ?= clang
# -ffp-contract=off: no fused multiply-add, so double arithmetic rounds exactly like the emulator's x87
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -fno-builtin -DDECOMP_HOOK
EMU = emu/x86.c emu/emu.c emu/crt.c emu/winapi.c emu/ole.c emu/registry.c emu/sapi4_tts.c
DECOMP = $(wildcard src/*.c)
HOOKS = harness/hooks.c harness/hooktab.c harness/adapters.c harness/guest.c harness/win32.c
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
	@echo "functions the portable build still needs (called through harness/guest.c):"; grep -o '0x636[0-9a-f]\{5\}' harness/guest.c | sort -u | tr '\n' ' '; echo; echo "rule procedures still run as the original:"; grep -o '0x[0-9a-f]*, PROC_GUEST' src/fe_procs.c | cut -d, -f1 | sort -u | tr '\n' ' '; echo
.PHONY: portable-check

# the portable build, first piece: the main lexicon with the C alone, against the original in the
# emulator (the word list: the system's, if there is one)
PORTC = src/fe_vm.c src/fe_procs.c src/fe_lex.c src/fe_rules.c src/vcrt.c src/crt_vc.c src/cfb.c
build/port_lex: port/lex.c port/dllimage.c port/dllimage.h port/lexdump.h $(PORTC) $(wildcard src/*.h)
	@mkdir -p build
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -fno-builtin -Isrc -Iport -o $@ port/lex.c port/dllimage.c $(PORTC) -lm
WORDS ?= /usr/share/dict/words
port-lex-check: build/port_lex build/unit
	@head -c 2000000 $(WORDS) | tr 'A-Z' 'a-z' | awk 'NR % 7 == 0' > build/port_words.txt
	@./build/unit data/msttsl --lexdump < build/port_words.txt > build/port_lex_ref.txt
	@./build/port_lex data/msttsl < build/port_words.txt > build/port_lex_out.txt
	@if cmp -s build/port_lex_ref.txt build/port_lex_out.txt; then echo "PORT-LEX: $$(wc -l < build/port_words.txt | tr -d ' ') words, $$(awk -F'\t' '$$2==1' build/port_lex_out.txt | wc -l | tr -d ' ') in the lexicon: portable output identical to the original"; else echo "PORT-LEX: DIFFERENT"; diff build/port_lex_ref.txt build/port_lex_out.txt | head; exit 1; fi
.PHONY: port-lex-check

# voice-baked variant: the DLL's data generated into build/dlldata_gen.c and compiled in
PORTCC = $(CC) -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -fno-builtin -Isrc -Iport
build/mkimage: port/mkimage.c port/dllimage.c port/dllimage.h
	@mkdir -p build
	$(PORTCC) -o $@ port/mkimage.c port/dllimage.c
build/dlldata_gen.c: build/mkimage data/msttsl/msttssyn.dll
	./build/mkimage data/msttsl > $@
build/port_lex_baked: port/lex.c port/dllimage.c port/dllimage.h port/lexdump.h build/dlldata_gen.c $(PORTC) $(wildcard src/*.h)
	$(PORTCC) -O1 -DDLL_BAKED -o $@ port/lex.c port/dllimage.c build/dlldata_gen.c $(PORTC) -lm
port-lex-baked-check: build/port_lex_baked port-lex-check
	@./build/port_lex_baked < build/port_words.txt > build/port_lex_baked_out.txt
	@if cmp -s build/port_lex_ref.txt build/port_lex_baked_out.txt; then echo "PORT-LEX (baked image): identical to the original"; else echo "PORT-LEX (baked): DIFFERENT"; exit 1; fi
.PHONY: port-lex-baked-check

# the portable front end: text to the unit stage's phone lists with the C alone, against the phone lists
# the original front end produces in the emulator (golden texts, plain)
PORTFE = $(PORTC) src/fe_small.c src/fe_word.c src/fe_phrase.c src/fe_output.c src/fe_text.c src/fe_lexer.c \
	src/fe_split.c src/fe_input.c src/fe_reader.c src/fe_phones.c src/fe_out.c src/fe_init.c src/queue.c src/tags.c src/containers.c src/fe_altpros.c
build/port_fe: port/fe.c port/dllimage.c port/dllimage.h $(PORTFE) $(wildcard src/*.h)
	@mkdir -p build
	$(PORTCC) -o $@ port/fe.c port/dllimage.c $(PORTFE) -lm
port-fe-check: build/port_fe build/sapi4_speak
	@./tools/port_fe_check.sh
.PHONY: port-fe-check
build/port_fe_baked: port/fe.c port/dllimage.c port/dllimage.h build/dlldata_gen.c $(PORTFE) $(wildcard src/*.h)
	$(PORTCC) -O1 -DDLL_BAKED -o $@ port/fe.c port/dllimage.c build/dlldata_gen.c $(PORTFE) -lm
port-fe-baked-check: build/port_fe_baked build/port_fe
	@printf '%s\n' "Hello, my name is Microsoft Sam." "The quick brown fox jumps over the lazy dog, and then it runs away into the forest." > build/port_fe_texts.txt
	@./build/port_fe data/msttsl < build/port_fe_texts.txt > build/port_fe_ref.txt
	@./build/port_fe_baked < build/port_fe_texts.txt > build/port_fe_baked_out.txt
	@if cmp -s build/port_fe_ref.txt build/port_fe_baked_out.txt; then echo "PORT-FE (baked image): identical to the image loaded from the DLL"; else echo "PORT-FE (baked): DIFFERENT"; exit 1; fi
.PHONY: port-fe-baked-check

# text to speech with the C alone (voices, front end, unit stage, synthesizer), against the golden renders
PORTALL = $(wildcard src/*.c)
build/port_render: port/render.c port/dllimage.c port/dllimage.h $(PORTALL) $(wildcard src/*.h)
	@mkdir -p build
	$(PORTCC) -o $@ port/render.c port/dllimage.c $(PORTALL) -lm
port-render-check: build/port_render
	@./tools/port_render_check.sh
.PHONY: port-render-check
build/port_render_baked: port/render.c port/dllimage.c port/dllimage.h build/dlldata_gen.c $(PORTALL) $(wildcard src/*.h)
	$(PORTCC) -O1 -DDLL_BAKED -o $@ port/render.c port/dllimage.c build/dlldata_gen.c $(PORTALL) -lm
port-render-baked-check: build/port_render_baked
	@./tools/port_render_check.sh build/port_render_baked
.PHONY: port-render-baked-check
port-tags-check: build/port_render build/sapi4_speak
	@./tools/port_tags_check.sh
.PHONY: port-tags-check
