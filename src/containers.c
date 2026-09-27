#include "containers.h"
#include "crt_vc.h"
#include "vcrt.h"
#include <string.h>

LAYOUT(HashTable, probes, 0x10);
LAYOUT(IntVec, data, 0x0c);
LAYOUT(VecArray, items, 0x10);
LAYOUT(Blob, bytes, 0x10);

int hash_lookup(HashTable *t, const char *key, int32_t *out) {
    if (!t || !key || !out) return -1;
    uint32_t h = 0;
    int k = -1;
    const char *p = key;
    signed char ch = (signed char)*p;
    do {
        h += (uint32_t)(int32_t)ch << (k & 15);
        k--;
        ch = (signed char)*++p;
    } while (ch);
    t->lookups++;
    for (;;) {
        if (!t->size) break;
        h %= t->size;
        const HashEntry *e = GP(const HashEntry, t->entries) + h;
        if (!e->key) break;
        if (vc_strcmp(GP(const char, e->key), key) == 0) {
            *out = e->value;
            return 0;
        }
        h++;
        t->probes++;
    }
    *out = (int32_t)h;
    return -1;
}

int named_index(const NamedTable *t, const char *key) {
    int32_t v;
    if (hash_lookup(GP(HashTable, t->names), key, &v)) return -1;
    return v;
}

GPTR(void) named_item(const NamedTable *t, int i) { return GP(GPTR(void), t->items)[i]; }

int prefix_cmp(const char *a, const char *b, int32_t *n) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (lb > la) return -1;
    if (lb < la) la = lb;
    *n = la;
    return vc_memcmp(a, b, (size_t)(uint32_t)la);
}

IntVec *intvec_ctor(IntVec *v) {
    v->cap = 0;
    v->data = 0;
    v->count = 0;
    v->step = 10;
    return v;
}

void intvec_free_data(IntVec *v) {
    if (v->data) vc_free(GP(void, v->data));
}

void intvec_reserve(IntVec *v, int n, int step) {
    if (n > v->cap) {
        GPSET(v->data, (int32_t *)vc_realloc(GP(void, v->data), (size_t)(uint32_t)(n * 4)));
        v->cap = n;
    }
    v->step = step > 0 ? step : 10;
}

VecArray *vecarray_ctor(VecArray *a) {
    memset(a, 0, sizeof *a);   // (a 0.0f and four zero words)
    return a;
}

void vecarray_destroy(VecArray *a) {
    for (int i = 0; i < a->count; i++) {
        IntVec *v = GP(IntVec, GP(GPTR(IntVec), a->items)[i]);
        if (!GP(GPTR(IntVec), a->items)[i]) continue;
        intvec_free_data(v);
        vc_free(v);
    }
    if (a->items) vc_free(GP(void, a->items));
    if (a->extra) vc_free(GP(void, a->extra));
}

int vecarray_init(VecArray *a, int n, int cap, int step) {
    a->count = n;
    GPTR(IntVec) *items = vc_malloc((size_t)(uint32_t)(n * 4));
    GPSET(a->items, items);
    if (!items) return -1;
    for (int i = 0; i < a->count; i++) {
        IntVec *v = vc_malloc(0x10);
        GPSET(items[i], v ? intvec_ctor(v) : NULL);
        if (!items[i]) {
            a->count = i;
            return -1;
        }
        intvec_reserve(GP(IntVec, items[i]), cap, step);
    }
    return 0;
}

int blob_copy80(const void *src, Blob *out) {
    out->bytes = 0x50;
    void *d = vc_malloc(0x50);
    GPSET(out->data, d);
    memcpy(d, src, (size_t)out->bytes);
    out->type = 4;
    return 1;
}
