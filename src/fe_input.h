// The front end's text input: the application's (wide) text made single-byte, its SAPI tags turned into
// the engine's escape sequences with a numbered entry for each bookmark, word position and tag value,
// then prepared and cut into sentences for the word module.
#pragma once
#include "gptr.h"
#include "fe_reader.h"

// one numbered entry (an escape "\x1bN<index> " in the text refers to it)
typedef struct FeEntry {
    int32_t pos;            // 0x00 (the slot after the last one allocated holds 2000000000)
    int32_t type;           // 0x04 1 word position, 0x13 bookmark, 0x15 its text, 0x14 its end, tags by code
    GPTR(char) text;        // 0x08 the value, as text (malloc)
} FeEntry;

// the front end object (only the fields the text input uses)
typedef struct FrontEnd {
    uint8_t _00[0x0c];
    GPTR(FeEntry) entries;  // 0x0c
    int32_t cap;            // 0x10
    int32_t last;           // 0x14 index of the last entry, -1 when none
    uint8_t _18[0x0c];
    int32_t words;          // 0x24 the word module's voice index
    int32_t f28;            // 0x28
    int32_t f2c;            // 0x2c
    uint8_t _30[4];
    GPTR(char) text;        // 0x34 the prepared text (malloc)
    GPTR(char) text_last;   // 0x38 its last character
    GPTR(char) cur;         // 0x3c where the word module is reading
    GPTR(char) next;        // 0x40 the next index escape ("\x1bI"), cut off with a NUL
    uint8_t _44[4];
    int32_t addr_mode;      // 0x48 2 inside an address tag
    uint8_t _4c[0x3c];
    int32_t single_char;    // 0x88 the text was one character (spoken as its name)
} FrontEnd;

void fe_entries_clear(FrontEnd *fe);                                    // @0x636803e4 thiscall
int32_t fe_entry_add_num(FrontEnd *fe, int32_t type, uint32_t value);   // @0x63680244 thiscall: its index
int32_t fe_entry_add_str(FrontEnd *fe, int32_t type, const char *s);    // @0x6368030a thiscall
GPTR(const char) tag_mode_name(int32_t v);                              // @0x63680f05 stdcall
int32_t tag_mode_value(const char *name);                               // @0x63680f2d stdcall
void tag_skip(GPTR(char) *pp);                                          // @0x636812b6 stdcall: past the closing backslash
void str_append(char **cur, const char *s);                             // @0x636812da stdcall: cur left on the NUL
// @0x6368043f thiscall: tags (when tags is set) into escapes; *out a new buffer (malloc); its length
int32_t fe_tags_to_escapes(FrontEnd *fe, int32_t tags, int32_t mode, char *text, GPTR(char) *out);
// @0x63680bb7 thiscall: keep a copy of the prepared text and hand its first part to the word module
int32_t fe_set_text(FrontEnd *fe, char *text);
// @0x636840f3 thiscall: one text from the application (mode 1: phonemes, converted first); *out what the
// word module returned; 0
int32_t fe_text_input(FrontEnd *fe, const uint16_t *text, int32_t *out, int32_t tags, int32_t mode);
// @0x636748d1 stdcall: phoneme symbols (1-based index in a table of 53) into their two-letter names,
// tag text copied as it is; *out a new wide buffer (calloc); 0, or -1 on an unknown symbol or a tag
// when tags are off
int32_t phonemes_to_names(const uint16_t *text, GPTR(uint16_t) *out, int32_t tags);
int32_t phoneme_index(uint16_t c);                                      // @0x63674978 stdcall
int32_t free_if2(void *p);                                              // @0x6367499a stdcall, as free_if

// not decompiled yet (harness/guest.c)
int32_t fe_after_feed(FrontEnd *fe, int32_t r);                         // @0x63680c6e thiscall
int32_t text_from_other(char *text, GPTR(char) *out);                   // @0x636744c4 stdcall
