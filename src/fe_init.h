// Setting up the front end (fe_init.c).
#pragma once
#include "gptr.h"
#include "fe_input.h"
#include "fe_reader.h"

// a word object: 0xb594 bytes; a portable build keeps its two list pointers in an extension (fe_phrase.h)
#ifdef DECOMP_HOOK
#define WOBJ_ALLOC 0xb594
#else
#define WOBJ_ALLOC (WOBJ_PTRS + 2 * sizeof(void *))
#endif

int32_t text_init(uint32_t unused, const char *cfg, uint32_t unused2);   // @0x6369aa68 stdcall
void fe_once(void);                                     // @0x6367f50a: text_init on the first call
void ev_list_init(uint8_t *w);                          // @0x6368abc1 stdcall
int32_t word_obj_init(uint8_t *w);                      // @0x63689051 cdecl
int32_t word_obj_create(void);                          // @0x63688803: a new word object; its index (1..128)
int32_t word_set_mask(uint8_t *w, uint8_t m);           // @0x6368b129 cdecl: the event kinds collected
int32_t word_set_events(int32_t idx, int32_t m);        // @0x636888e1 cdecl
int32_t word_set_mode(int32_t idx, int32_t v);          // @0x63688a30 cdecl
int32_t word_set_rate(int32_t idx, int32_t v);          // @0x63688bc2 cdecl
void user_lex_builtin(GPTR(UserLex) *out);              // @0x636750ea stdcall: from the DLL's own list
void user_lex_file(GPTR(UserLex) *out);                 // @0x6367518a stdcall: from the user's file
int32_t at_end(const char *s);                          // @0x63676e7e stdcall
void read_line(char *dst, int32_t n, char **src);       // @0x63676e8c stdcall
void words_set_f624(int32_t idx, int32_t v);            // @0x63677ae1 stdcall
// @0x63677045 stdcall: a word module for a voice (mode 0: the alternative prosody, its data text); the index
int32_t words_open(int32_t mode, char *data);
void fe_entries_init(FrontEnd *fe);                     // @0x636803b5 thiscall
FrontEnd *fe_ctor(FrontEnd *fe);                        // @0x636834d4 thiscall
// @0x636835bc thiscall: queues, the voice description (+0x8d8 its pitch), window and message; 1
int32_t fe_setup(FrontEnd *fe, uint8_t *queue_in, uint8_t *queue_out, const uint8_t *voice, int32_t voice_idx,
                 int32_t f50, int32_t unused, uint32_t hwnd, uint32_t msg, int32_t f8c);
// not decompiled yet (harness/guest.c): the user lexicon file reader
int32_t user_lex_load(UserLex *t);                      // @0x636751e0 stdcall
