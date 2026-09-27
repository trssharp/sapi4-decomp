#include "containers.h"
#include "crt_vc.h"
#include "vcrt.h"
#include "x87.h"
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
    if (a->result) vc_free(GP(void, a->result));
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

int32_t intvec_append(IntVec *v, int32_t x) {
    if (v->count >= v->cap) {
        v->cap += v->step;
        GPSET(v->data, (int32_t *)vc_realloc(GP(void, v->data), (size_t)(uint32_t)(v->cap * 4)));
    }
    GP(int32_t, v->data)[v->count] = x;
    return v->count++;
}

int32_t vecarray_append(VecArray *a, int32_t i, int32_t x) {
    if (i > a->count || i < 0) return -1;
    intvec_append(GP(IntVec, GP(GPTR(IntVec), a->items)[i]), x);
    return 0;
}

double lattice_cost(int32_t a, int32_t b) { return a == b ? 0.0 : 1.0; }

int array_lacks(int16_t v, int32_t n, const int16_t *a) {
    for (int32_t i = 0; i < n; i++)
        if (a[i] == v) return 0;
    return 1;
}

static double cost(uint32_t fn, int32_t a, int32_t b) {
    if (CODEADDR(fn) == 0x63685d00) return lattice_cost(a, b);
    return lattice_cost_other(fn, a, b);
}

int32_t lattice_viterbi(VecArray *a, uint32_t fn) {
    if (fn) a->cost_fn = fn;
    int32_t ret = -1;
    GPTR(IntVec) const *items = GP(GPTR(IntVec), a->items);
#define ITEM(t) GP(IntVec, items[t])
    int32_t n0 = ITEM(0)->count, mx = n0 ? n0 : 1, total = mx;
    for (int32_t t = 1; t < a->count; t++) {
        int32_t c = ITEM(t)->count ? ITEM(t)->count : 1;
        if (c > mx) mx = c;
        total += c;
    }
    float *score = vc_calloc((size_t)(uint32_t)total, 4);
    int32_t *back = vc_calloc((size_t)(uint32_t)total, 4);
    GPTR(float) *S = vc_malloc((size_t)(uint32_t)(a->count * 4));
    GPTR(int32_t) *B = vc_malloc((size_t)(uint32_t)(a->count * 4));
    if (!S || !score || !B || !back) goto done;
    GPSET(S[0], score);
    GPSET(B[0], back);
    int32_t n = n0, off = n0 ? n0 : 1, best = 0;
    for (int32_t t = 1; t < a->count; t++) {
        int32_t prevn = n;
        const IntVec *cur = ITEM(t);
        n = cur->count;
        GPSET(S[t], score + off);
        GPSET(B[t], back + off);
        off += n ? n : 1;
        for (int32_t j = 0; j < n; j++) {
            for (int32_t k = 0; k < prevn; k++) {
                const IntVec *prev = ITEM(t - 1);
                if (GP(int32_t, prev->data)[k] == 0) continue;
                double c = cost(a->cost_fn, GP(int32_t, prev->data)[k], GP(int32_t, ITEM(t)->data)[j]);
                double v = c + GP(float, S[t - 1])[k];
                float *s = GP(float, S[t]) + j;
                if (!x87_jae(v, *s)) {
                    *s = (float)v;
                    GP(int32_t, B[t])[j] = k;
                }
            }
            if (!x87_je(0.0, a->beam) && !x87_jbe(GP(float, S[t])[j], a->beam))
                GP(int32_t, ITEM(t)->data)[j] = 0;
        }
    }
    {
        const float *last = GP(float, S[a->count - 1]);
        double st = last[0];
        for (int32_t i = 1; i < n; i++)
            if (!x87_jbe(st, last[i])) { best = i; st = last[i]; }
    }
    int32_t *res = vc_calloc((size_t)(uint32_t)a->count, 4);
    GPSET(a->result, res);
    if (!res) goto done;
    {
        int32_t c = a->count;
        const IntVec *lastv = ITEM(c - 1);
        res[c - 1] = best >= lastv->count ? 0 : GP(int32_t, lastv->data)[best];
        int32_t pick = best;
        for (int32_t t = c - 2; t >= 0; t--) {
            if (ITEM(t + 1)->count != 0) {
                pick = GP(int32_t, B[t + 1])[pick];
                best = pick;
            } else {
                int32_t m = ITEM(t)->count;
                const float *sc = GP(float, S[t]);
                double st = sc[0];
                if (m > 1) {
                    for (int32_t i = 1; i < m; i++)
                        if (!x87_jbe(st, sc[i])) { best = i; st = sc[i]; }
                    pick = best;
                }
            }
            const IntVec *v = ITEM(t);
            res[t] = pick >= v->count ? 0 : GP(int32_t, v->data)[pick];
        }
    }
    ret = 0;
done:
    if (S) vc_free(S);
    if (score) vc_free(score);
    if (B) vc_free(B);
    if (back) vc_free(back);
    return ret;
#undef ITEM
}
