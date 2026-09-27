// The unit stage of msttssyn.dll (its own worker thread): phones -> phone indices -> senones, then
// durations and pitch. The stage object is large; only the fields decompiled code touches are named.
#pragma once
#include "gptr.h"
#include "containers.h"
#include "senone.h"

typedef struct UnitRec {       // one phone of the phrase (0x4c bytes)
    int32_t phone;             // 0x00 phone index
    int32_t senone;            // 0x04
    float dur;                 // 0x08 duration factor (0: none given)
    int32_t nstates;           // 0x0c
    float a[5];                // 0x10 per state (copied through)
    float b[5];                // 0x24 per state (rounded to int16 on output)
    float c[5];                // 0x38 per state (scaled on output)
} UnitRec;

// a phone as the front end hands it over (100 bytes)
typedef struct PhoneIn {
    char name[16];             // 0x00
    float dur;                 // 0x10
    int32_t nstates;           // 0x14
    float a[5], b[5], c[5];    // 0x18, 0x2c, 0x40
    uint8_t _54[0x10];
} PhoneIn;

// a unit as the stage hands it to the synthesizer (0x50 bytes)
typedef struct UnitOut {
    char name[16];             // 0x00
    int16_t unit;              // 0x10 unit index in the voice (0: silence)
    int16_t _12;
    float dur;                 // 0x14
    int32_t nstates;           // 0x18
    float a[5];                // 0x1c
    int16_t b[5];              // 0x30
    int16_t _3a;
    float c[5];                // 0x3c
} UnitOut;

typedef struct UnitOutBuf { GPTR(UnitOut) units; int32_t bytes; } UnitOutBuf;

typedef struct SenStat { float scale; float _4, _8; float var; } SenStat;   // per senone

typedef struct UnitStage {
    int32_t _000;
    GPTR(const SenStat) stats[150];   // 0x004 per phone: per-senone statistics
    GPTR(UnitRec) recs;        // 0x25c
    GPTR(int16_t) ids;         // 0x260 phone index per unit
    GPTR(int16_t) senones;     // 0x264 senone per unit
    int32_t cap;               // 0x268 capacity of the three arrays
    uint8_t _26c[0x280 - 0x26c];
    float dur_scale;           // 0x280
    float sil_dur;             // 0x284
    int32_t final_sil;         // 0x288 append a silence unit when the next phrase does not start with one
    uint32_t amp;              // 0x28c output scale of c[] (x 1/65535)

    GPTR(const NamedTable) phone_names;   // 0x290
    GPTR(const SenoneTree) tree;          // 0x294
    int32_t tree_mode;         // 0x298 1: senone_lookup; otherwise the older query (0x6367233c)
    int32_t senone_base;       // 0x29c
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

// @0x63686c7d, @0x63686905 thiscall (not decompiled yet)
void unit_prosody_a(UnitStage *u, int n);
void unit_prosody_b(UnitStage *u, int n);

// @0x636871a4 thiscall: durations and state values of the n units just selected (from the phones'
// own values and the per-senone statistics), then the two contour passes, then the output units
// (plus a final silence if configured and the next phone is not silence). *last = the last phone.
int unit_prosody(UnitStage *u, const PhoneIn *ph, int n, int32_t *last, int next_phone, UnitOutBuf *out);

// @0x63687751 thiscall: look up the phone index and the senone of every phone of a phrase. ph: n names
// of 100 bytes; "\..." (tags) and "#..." (boundaries) are not phones, they mark word edges.
void unit_select(UnitStage *u, const char *ph, int n);
