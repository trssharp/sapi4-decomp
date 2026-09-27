// The front end's rule machine. msttssyn.dll contains three copies of it (compiled from the same
// source with different globals): six byte tapes, a stack index into them and a program pointer into
// the rule bytecode. One C function per routine serves all three; the instance says which globals.
#pragma once
#include "gptr.h"

typedef struct RuleVM {
    uint32_t tape[6];       // globals holding the six tapes (char *)
    uint32_t top;           // int16 global: the stack top on the tapes
    uint32_t pc;            // global holding the bytecode pointer (unsigned char *)
} RuleVM;
extern const RuleVM rule_vm[3];

// @0x6368d725 (and copies): open a gap: tape[i] = tape[i-1] for i = hi..lo, then mark both ends
// (0x0e on tapes 0-4, 0 on tape 5)
void vm_shift_right(const RuleVM *vm, int16_t lo, int16_t hi);
// @0x6368d822: close a gap: tape[i-2] = tape[i-1] for i = lo..hi. Returns what the original leaves in
// EAX (callers pass it on): hi + 1, or lo with prev_eax's upper half when there is nothing to move.
uint32_t vm_shift_left(const RuleVM *vm, int16_t lo, int16_t hi, uint32_t prev_eax);
// @0x6368f4b2: move b..a (downwards) onto the stack below the top
void vm_pop_range(const RuleVM *vm, int16_t a, int16_t b);
// @0x6368f68c: copy from the stack back to s+1.. until a 0x0e marker; returns the last position
int32_t vm_push_until(const RuleVM *vm, int32_t s);
// @0x6368e4cf: skip the bytecode block opened by `tok` (nesting on tok,1 and 0xf5 pairs) up to tok,end;
// 1 in AL if found, 0 at the tok,0 terminator. Returns the full EAX of the original.
uint32_t vm_skip_block(const RuleVM *vm, uint8_t tok, uint8_t end);
// @0x6368f567: move tape positions a-1..b by d (d > 0: towards the end, clearing tape 5 behind; d <= 0:
// towards the start, adding tape 5 into the destination). Returns b + d. (32-bit arguments of which
// the original uses the low halves in places.)
int32_t vm_move(const RuleVM *vm, int32_t a, int32_t b, int32_t d);
