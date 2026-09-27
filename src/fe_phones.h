// The front end's phrase output: the word object's tapes and output string copied out, the output
// string parsed into phones with durations and pitch targets, and the phone list the unit stage takes
// (PhoneIn records: phone name with stress digit, duration factor, pitch).
#pragma once
#include "gptr.h"
#include "fe_reader.h"
#include "unitsel.h"

// @0x636761ab cdecl: five tapes of word object idx (0x585, 0xb05, 0x1085, 0x1605, 0x438e) copied into
// new strings
void tapes_copy(int32_t idx, GPTR(char) *a, GPTR(char) *b, GPTR(char) *c, GPTR(char) *d, GPTR(char) *e);
// @0x63677b56 stdcall: the phrase's strings, phone positions and index marks; 1 if a copy failed
int32_t phrase_init(Prosody *p, const char *s1, const char *s2, const char *s3, const char *s4, const char *s5,
                    const char *s6, const char *s7, const char *s8, const char *s0, int32_t nev,
                    const uint8_t *ev, int32_t *marks);
// @0x636765ea stdcall: the output string ("...(t,v)...]p[d]p[d]...") into pitch targets and phones with
// durations; *pitch the per-phone pitch (interpolated), *phones the phone characters; the phone count
int32_t phrase_parse(const char *s, GPTR(float) *pitch, GPTR(char) *phones, Prosody *p);
double pitch_value(int32_t mode, int32_t base, double x);      // @0x636765a5 stdcall
GPTR(const char) phone_name(uint8_t code);                      // @0x63675f6b stdcall
int32_t name_is_vowel(const char *name);                         // @0x63676130 stdcall
int32_t code_is_vowel(char code);                                // @0x63676159 stdcall
// @0x6367678b stdcall: the phone list (count_only: just count); the number of records
int32_t phone_list(int32_t idx, const char *text, const char *phones, const float *pitch, PhoneIn *out,
                   int32_t count_only, int32_t final, const char *s7, const char *s1);
int32_t words_phrase(int32_t idx);                               // @0x6367764f stdcall
void phrase_free(Prosody *p, int32_t mode);                      // @0x63677da6 stdcall
int32_t phones_build(int32_t idx, PhoneIn *out, int32_t final, int32_t unused);  // @0x636778a7 stdcall
void words_abort(int32_t idx);                                   // @0x63677911 stdcall
char *str_dup0(const char *s);                                   // @0x6368021c stdcall (calloc)
void *calloc_n(uint32_t n, uint32_t size);                       // @0x63674478 stdcall: NULL when n is 0

// not decompiled yet (harness/guest.c): the alternative prosody (mode 0)
void pros_alt_a(Prosody *p);                                     // @0x636798ce
void pros_alt_b(Prosody *p);                                     // @0x6367930a
void pros_alt_c(Prosody *p);                                     // @0x636787f9
void pros_alt_d(GPTR(float) *tmp, Prosody *p);                   // @0x6367877e
