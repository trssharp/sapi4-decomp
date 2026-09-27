// Senone selection of msttssyn.dll: the decision trees (TreeImage stream) that map a phone in its
// left/right context, word position and HMM state to a senone. Layouts are the original's; fields
// not read by the decompiled code are padding.
#pragma once
#include "gptr.h"

typedef struct SenoneHeader {
    int32_t _00;
    int32_t right_base;     // 0x04 word offset of the right-context bits in a question row
    int32_t _08[6];
    int32_t bit_offset;     // 0x20 bit offset of the phone bits (nonzero: rows also test the state)
} SenoneHeader;

typedef struct PhoneSet {
    int32_t _00, _04;
    GPTR(GPTR(const char)) names;   // 0x08
    int32_t _0c[7];
    int32_t count;          // 0x28
} PhoneSet;

typedef struct TreeNode { uint16_t qlist; int16_t yes; int16_t no; int16_t _6; } TreeNode;   // yes < 0: leaf, value in no
typedef struct TreeRoot { uint16_t present; uint16_t _2; GPTR(const TreeNode) nodes; } TreeRoot;

typedef struct SenoneTree {
    GPTR(const SenoneHeader) hdr;   // 0x00
    GPTR(const PhoneSet) phones;    // 0x04
    int32_t _08[3];
    int32_t ignore_position;        // 0x14
    int32_t sil;                    // 0x18 phone used for silence / "+" contexts
    int32_t word_end_right;         // 0x1c right context at a word end (< 0: keep)
    uint32_t flags;                 // 0x20 bit 1: questions also test the centre phone
    int32_t _24[5];
    GPTR(const int8_t) nstates;     // 0x38 per model
    int32_t _3c;
    GPTR(const int8_t) model;       // 0x40 per phone
    int32_t _44[2];
    GPTR(GPTR(const TreeRoot)) roots;  // 0x4c per model, per state
    int32_t _50;
    GPTR(const uint32_t) qbits;     // 0x54 question rows
    int32_t qstride;                // 0x58 words per row
    int32_t _5c;
    GPTR(const uint16_t) qlists;    // 0x60 question lists, 0xffff-terminated
} SenoneTree;

// @0x63674184 stdcall: senone of phone `cur` between `left` and `right` in HMM state `state`; `pos` is
// the position in the word ('b'egin, 'e'nd, 's'ingle, 0 = middle; either case). -2 for bad arguments,
// -1 when the model has no tree for that state.
int senone_lookup(const SenoneTree *t, int cur, int left, int right, int pos, int state);

// @0x636876b2 stdcall: phone fix-ups before selection: DX -> T, and the unstressed diphthongs
// AW0 AY0 EY0 OY0 -> stressed. ph: n names of 100 bytes each.
void phone_fixups(char *ph, int n);
