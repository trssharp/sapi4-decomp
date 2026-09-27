// The unit stage of msttssyn.dll (its own worker thread): phones -> phone indices -> senones, then
// durations and pitch. The stage object is large; only the fields decompiled code touches are named.
#pragma once
#include "gptr.h"
#include "containers.h"
#include "senone.h"

typedef struct UnitRec {       // one phone of the phrase (0x4c bytes)
    int32_t phone;             // 0x00 phone index
    int32_t senone;            // 0x04
    uint8_t _08[0x44];
} UnitRec;

typedef struct UnitStage {
    uint8_t _000[0x25c];
    GPTR(UnitRec) recs;        // 0x25c
    GPTR(int16_t) ids;         // 0x260 phone index per unit
    GPTR(int16_t) senones;     // 0x264 senone per unit
    int32_t cap;               // 0x268 capacity of the three arrays
    uint8_t _26c[0x290 - 0x26c];
    GPTR(const NamedTable) phone_names;   // 0x290
    GPTR(const SenoneTree) tree;          // 0x294
    int32_t tree_mode;         // 0x298 1: senone_lookup; otherwise the older query (0x6367233c)
    int32_t _29c;
    int32_t sil_phone;         // 0x2a0 phone index used outside the phrase
} UnitStage;

// the query of the older tree interface (0x6367233c, not decompiled; never reached by the corpus)
typedef struct TreeQuery {
    int16_t cur, left, right;  // 0x00
    uint8_t _06[4];
    int8_t pos;                // 0x0a
    uint8_t _0b[0x21];
    int8_t _2c;                // 0x2c set to 0
    uint8_t _2d[3];
} TreeQuery;
// @0x6367233c stdcall (not decompiled yet: the hook build calls the original)
int32_t tree_query(const SenoneTree *t, TreeQuery *q);

// @0x63687751 thiscall: look up the phone index and the senone of every phone of a phrase. ph: n names
// of 100 bytes; "\..." (tags) and "#..." (boundaries) are not phones, they mark word edges.
void unit_select(UnitStage *u, const char *ph, int n);
