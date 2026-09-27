// The Microsoft C runtime (MSVCRT, VC 5 era) behaviour the engine depends on, reproduced exactly:
// where the algorithm itself shows in the output (qsort's order of equal elements, rand's sequence).
#pragma once
#include <stddef.h>
#include <stdint.h>
// qsort exactly as the VC CRT does it (median-less quicksort, shortsort below 9 elements): elements
// that compare equal end up where the original put them.
void vc_qsort(void *base, size_t num, size_t width, int (*cmp)(const void *, const void *));
// rand() as MSVCRT has it (per-thread state, seed 1): the effects' RoboSoft warble depends on the
// exact sequence. In the hook build this advances the calling guest thread's own state.
int vc_rand(void);
// malloc / free on the engine's heap (in the hook build the guest heap, so the original code can use
// what the decompiled code allocates, in the same order and at the same addresses)
void *vc_malloc(size_t n);
void vc_free(void *p);
// KERNEL32 Sleep: the engine yields its worker threads with Sleep(0). Nothing to do in a single-threaded
// build; the hook build advances the emulator's virtual clock and yields when the hook returns.
void vc_sleep(uint32_t ms);
#ifdef DECOMP_HOOK
void decomp_warn(const char *msg);
#endif
