// Small containers of msttssyn.dll's unit stage: a string hash table, growable arrays, a byte blob.
// Layouts are the original's.
#pragma once
#include "gptr.h"

typedef struct HashEntry { int32_t value; GPTR(const char) key; } HashEntry;
typedef struct HashTable {
    uint32_t size;          // 0x00 buckets
    int32_t _04;
    GPTR(HashEntry) entries;  // 0x08
    int32_t lookups;        // 0x0c statistics
    int32_t probes;         // 0x10
} HashTable;

// @0x63675e95 stdcall: find key (hash: sum of char << (15, 14, ... 0, 15, ...), linear probing, strcmp).
// Found: *out = value, returns 0. Not found: *out = the free bucket, returns -1 (also on NULL args).
int hash_lookup(HashTable *t, const char *key, int32_t *out);

typedef struct NamedTable { int32_t _00; GPTR(HashTable) names; GPTR(GPTR(void)) items; } NamedTable;
// @0x6367fb1e stdcall: index of a name, or -1
int named_index(const NamedTable *t, const char *key);
// @0x6367fb43 stdcall: item i
GPTR(void) named_item(const NamedTable *t, int i);

// @0x63685bb9 stdcall: compare the first min(strlen) bytes of a and b (memcmp sign), storing that
// length in *n; -1 at once if b is longer than a
int prefix_cmp(const char *a, const char *b, int32_t *n);

typedef struct IntVec {
    int32_t cap;            // 0x00
    int32_t count;          // 0x04
    int32_t step;           // 0x08 growth step (10)
    GPTR(int32_t) data;     // 0x0c
} IntVec;
// @0x63688151 thiscall: constructor
IntVec *intvec_ctor(IntVec *v);
// @0x63688165 thiscall: free the data (the vector itself is freed by the caller)
void intvec_free_data(IntVec *v);
// @0x63688175 thiscall: make room for n (realloc when n > cap), set the growth step (<= 0: 10)
void intvec_reserve(IntVec *v, int n, int step);

typedef struct VecArray {
    float beam;             // 0x00 prune candidates scoring above this (0: off)
    uint32_t cost_fn;       // 0x04 code address of the transition cost function (cdecl, ST0)
    GPTR(int32_t) result;   // 0x08 best path, one candidate per stage (freed by vecarray_destroy)
    int32_t count;          // 0x0c
    GPTR(GPTR(IntVec)) items;  // 0x10
} VecArray;
// @0x636881ed thiscall: constructor (the first field is a float 0.0)
VecArray *vecarray_ctor(VecArray *a);
// @0x63688202 thiscall: free every vector, the array, and extra
void vecarray_destroy(VecArray *a);
// @0x6368824c thiscall: n vectors, each reserved (cap, step); 0, or -1 when out of memory (count is
// then the number made). C++ with an exception frame in the original; nothing here throws.
int vecarray_init(VecArray *a, int n, int cap, int step);

typedef struct Blob { int32_t type; int32_t _04, _08; GPTR(void) data; int32_t bytes; } Blob;
// @0x63685814 stdcall: copy 0x50 bytes into a new blob of type 4; returns 1
int blob_copy80(const void *src, Blob *out);

// @0x636881b0 thiscall: append, growing by `step`; returns the old count
int32_t intvec_append(IntVec *v, int32_t x);
// @0x636882d6 thiscall: append x to vector i (0..count); 0, or -1 if i is out of range
int32_t vecarray_append(VecArray *a, int32_t i, int32_t x);
// @0x63685d00 cdecl: the lattice's transition cost: 0.0 between equal candidates, else 1.0 (ST0)
double lattice_cost(int32_t a, int32_t b);
// @0x63685bf7 stdcall: 0 if v occurs in a[0..n-1], else 1
int array_lacks(int16_t v, int32_t n, const int16_t *a);
// @0x636882fc thiscall: Viterbi search over the candidate vectors (one per stage) with the cost
// function (cost_fn replaced when fn is nonzero); the best path into `result`. 0, or -1 when out of
// memory.
int32_t lattice_viterbi(VecArray *a, uint32_t fn);
// a cost function that is not lattice_cost (hook build: runs the original)
double lattice_cost_other(uint32_t fn, int32_t a, int32_t b);
