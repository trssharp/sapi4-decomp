// The front end's rule modules. The DLL contains ten or so modules generated from the same templates
// (a multi-tape rewriting machine: four byte tapes shared by all modules, per-module state in .data),
// so one C function serves every instance; the instance is identified by its globals.
#pragma once
#include "gptr.h"

// the four shared tapes (global char pointers in .data)
#define TAPE_A 0x63739e7c
#define TAPE_B 0x63739e6c
#define TAPE_C 0x63739e78
#define TAPE_D 0x63739e70

// @0x63697558 (and 8 identical copies) stdcall: 1 if tape A[lo..hi] are all graphic (0x21..0xfe)
int tapes_graphic(int16_t lo, int16_t hi);
// @0x63697583 (and 9 more instances, one per module) stdcall: pad the four tapes with '~' up to the
// furthest of their ends (ends: the module's int16[4]) and a; returns that position. (Its second
// argument is overwritten before use.)
int16_t tapes_pad(const int16_t *ends, int16_t a);
