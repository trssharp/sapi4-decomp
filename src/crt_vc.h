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
void *vc_realloc(void *p, size_t n);
// KERNEL32 Sleep: the engine yields its worker threads with Sleep(0). Nothing to do in a single-threaded
// build; the hook build advances the emulator's virtual clock and yields when the hook returns.
void vc_sleep(uint32_t ms);
#ifdef DECOMP_HOOK
void decomp_warn(const char *msg);
#endif

// The Win32 synchronisation and messaging calls the engine's thread plumbing makes. Handles and window
// handles are opaque 32-bit values. The hook build performs exactly what the emulator's KERNEL32/USER32
// do (blocking ones park the calling guest thread: only from HOOKY hooks); a single-threaded portable
// build gets trivial versions.
void vc_EnterCriticalSection(void *cs);
void vc_LeaveCriticalSection(void *cs);
int32_t vc_SetEvent(uint32_t h);
int32_t vc_ResetEvent(uint32_t h);
uint32_t vc_WaitForSingleObject(uint32_t h, uint32_t ms);
uint32_t vc_WaitForMultipleObjects(uint32_t n, const uint32_t *hs, int all, uint32_t ms);
int32_t vc_PostMessageA(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
int32_t vc_SetThreadPriority(uint32_t h, int32_t prio);
void vc_DebugBreak(void);
// IUnknown::Release on a COM object handed over by the SAPI layer (hook build: the guest object)
uint32_t vc_com_release(uint32_t obj);
