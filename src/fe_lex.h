// The front end's lexicon modules: ten dictionaries (the rule procedures of rule machine C that look a
// word up), each with its own tables, and their shared helpers.
#pragma once
#include "gptr.h"

// the letter codes of the word being looked up (bytes at 0x63739e00) and the packed search key built
// from them (0x63739e40, compared by rule_bsearch)
#define LEX_CODES 0x63739e00
#define LEX_KEY 0x63739e40

// @0x6369a9c8 stdcall: pack `count` letter codes of `bits` bits each (from LEX_CODES) into LEX_KEY,
// most significant bit first. Returns what the original leaves in EAX (the last 16-bit window).
uint32_t lex_pack(uint8_t bits, uint16_t count);

// @0x6369a87e and 7 more, one per dictionary (n = 9..13): entry idx of an array of n-bit fields packed
// into 16-bit words (most significant first) at `table`; the low 16 bits are the field (the rest of EAX
// as the original leaves it)
uint32_t lex_field(uint32_t table, uint16_t idx, int n);
// @0x6369a47b: the same for 18-bit fields (the dictionary with its table in .data at 0x63722090)
uint32_t lex_field18(uint16_t idx);
