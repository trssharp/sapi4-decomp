#include "crt_vc.h"
#include <stdint.h>

static void swap_elems(char *a, char *b, size_t w) {
    if (a == b) return;
    for (size_t i = 0; i < w; i++) { char t = a[i]; a[i] = b[i]; b[i] = t; }
}
static void shortsort(char *lo, char *hi, size_t w, int (*cmp)(const void *, const void *)) {
    while (hi > lo) {
        char *max = lo;
        for (char *p = lo + w; p <= hi; p += w)
            if (cmp(p, max) > 0) max = p;
        swap_elems(max, hi, w);
        hi -= w;
    }
}
void vc_qsort(void *base, size_t num, size_t w, int (*cmp)(const void *, const void *)) {
    if (num < 2 || w == 0) return;
    char *lostk[32], *histk[32];
    int sp = 0;
    char *lo = base, *hi = (char *)base + w * (num - 1);
recurse:;
    size_t size = (size_t)(hi - lo) / w + 1;
    if (size <= 8) {
        shortsort(lo, hi, w, cmp);
    } else {
        char *mid = lo + (size / 2) * w;
        swap_elems(mid, lo, w);
        char *loguy = lo, *higuy = hi + w;
        for (;;) {
            do { loguy += w; } while (loguy <= hi && cmp(loguy, lo) <= 0);
            do { higuy -= w; } while (higuy > lo && cmp(higuy, lo) >= 0);
            if (higuy < loguy) break;
            swap_elems(loguy, higuy, w);
        }
        swap_elems(lo, higuy, w);
        if (higuy - 1 - lo >= hi - loguy) {
            if (lo + w < higuy) { lostk[sp] = lo; histk[sp] = higuy - w; ++sp; }
            if (loguy < hi) { lo = loguy; goto recurse; }
        } else {
            if (loguy < hi) { lostk[sp] = loguy; histk[sp] = hi; ++sp; }
            if (lo + w < higuy) { hi = higuy - w; goto recurse; }
        }
    }
    --sp;
    if (sp >= 0) { lo = lostk[sp]; hi = histk[sp]; goto recurse; }
}

#ifndef DECOMP_HOOK
#include <stdlib.h>
void *vc_malloc(size_t n) { return malloc(n); }
void vc_free(void *p) { free(p); }
static _Thread_local uint32_t vc_seed = 1;
int vc_rand(void) {
    vc_seed = vc_seed * 214013u + 2531011u;
    return (int)((vc_seed >> 16) & 0x7fff);
}
#endif
