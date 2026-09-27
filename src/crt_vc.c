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
void *vc_realloc(void *p, size_t n) { return realloc(p, n); }
void *vc_calloc(size_t n, size_t size) { return calloc(n, size); }
void vc_sleep(uint32_t ms) { (void)ms; }
void *vc_stack_push(uint32_t size, uint32_t below) { char *p = malloc((size_t)size + below); return p ? p + below : NULL; }
void vc_stack_pop(void *frame, uint32_t size, uint32_t below) { (void)size; free((char *)frame - below); }
void vc_EnterCriticalSection(void *cs) { (void)cs; }
void vc_LeaveCriticalSection(void *cs) { (void)cs; }
// events of a single-threaded build: set / reset flags; a wait on nothing set times out (0x102)
static struct { int used, manual, set; } vc_events[64];
uint32_t vc_CreateEventA(int32_t manual, int32_t initial) {
    for (uint32_t i = 1; i < 64; i++)
        if (!vc_events[i].used) {
            vc_events[i].used = 1;
            vc_events[i].manual = manual != 0;
            vc_events[i].set = initial != 0;
            return 0x100 + i;
        }
    return 0;
}
void vc_InitializeCriticalSection(void *cs) { (void)cs; }
static int vc_event(uint32_t h) { return h > 0x100 && h < 0x100 + 64 && vc_events[h - 0x100].used ? (int)(h - 0x100) : 0; }
int32_t vc_SetEvent(uint32_t h) { int i = vc_event(h); if (i) vc_events[i].set = 1; return i != 0; }
int32_t vc_ResetEvent(uint32_t h) { int i = vc_event(h); if (i) vc_events[i].set = 0; return i != 0; }
uint32_t vc_WaitForMultipleObjects(uint32_t n, const uint32_t *hs, int all, uint32_t ms) {
    (void)ms;
    if (all) {
        for (uint32_t k = 0; k < n; k++) { int i = vc_event(hs[k]); if (!i || !vc_events[i].set) return 0x102; }
        for (uint32_t k = 0; k < n; k++) { int i = vc_event(hs[k]); if (!vc_events[i].manual) vc_events[i].set = 0; }
        return 0;
    }
    for (uint32_t k = 0; k < n; k++) {
        int i = vc_event(hs[k]);
        if (i && vc_events[i].set) {
            if (!vc_events[i].manual) vc_events[i].set = 0;
            return k;
        }
    }
    return 0x102;       // (nothing else can set it: a wait would never end)
}
uint32_t vc_WaitForSingleObject(uint32_t h, uint32_t ms) { return vc_WaitForMultipleObjects(1, &h, 0, ms); }
int32_t vc_PostMessageA(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) { (void)hwnd; (void)msg; (void)wp; (void)lp; return 1; }
int32_t vc_SetThreadPriority(uint32_t h, int32_t prio) { (void)h; (void)prio; return 1; }
void vc_DebugBreak(void) {}
uint32_t vc_com_release(void *obj) { (void)obj; return 0; }
int32_t vc_CoInitialize(void) { return 0; }
void vc_CoUninitialize(void) {}
int32_t vc_CoCreateInstance(uint32_t clsid, uint32_t ctx, uint32_t iid, void **out) {
    (void)clsid; (void)ctx; (void)iid;
    *out = NULL;
    return (int32_t)0x80040154u;
}
int32_t vc_com_call(void *obj, int slot, int nargs, const uint32_t *args) {
    (void)obj; (void)slot; (void)nargs; (void)args;
    return (int32_t)0x80004005u;
}
static _Thread_local uint32_t vc_seed = 1;
int vc_rand(void) {
    vc_seed = vc_seed * 214013u + 2531011u;
    return (int)((vc_seed >> 16) & 0x7fff);
}
#endif
