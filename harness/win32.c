// The decompiled code's Win32 calls in the hook build: non-blocking ones run the emulator's own
// implementation through the DLL's import table (so they are the very same code), blocking ones use
// the emulator's wait / critical-section logic and park the coroutine instead of stopping the CPU.
#include "hooks.h"
#include "crt_vc.h"
#include "gptr.h"

static uint32_t api(uint32_t iat, int nargs, const uint32_t *args) {
    uint32_t thunk = *DLLVAR(uint32_t, iat);
    return emu_call(decomp_emu, thunk, nargs, args);
}
static uint32_t g(const void *p) { return p ? (uint32_t)((const uint8_t *)p - decomp_guest_mem) : 0; }
static void must_block(const char *what) {
    if (!decomp_in_coro()) x86_fault(emu_cpu(decomp_emu), "decomp: %s would block outside a HOOKY hook", what);
}

void vc_EnterCriticalSection(void *cs) {
    for (;;) {
        if (emu_cs_try_enter(decomp_emu, g(cs))) return;
        must_block("EnterCriticalSection");
        Thread *t = decomp_emu->cur;
        t->state = T_BLOCKED;
        t->gen = decomp_emu->gen;
        t->wake_at = 0;
        decomp_coro_block();
    }
}
void vc_LeaveCriticalSection(void *cs) { uint32_t a[1] = { g(cs) }; api(0x6369e014, 1, a); }
int32_t vc_SetEvent(uint32_t h) { uint32_t a[1] = { h }; return (int32_t)api(0x6369e038, 1, a); }
int32_t vc_ResetEvent(uint32_t h) { uint32_t a[1] = { h }; return (int32_t)api(0x6369e088, 1, a); }
uint32_t vc_WaitForMultipleObjects(uint32_t n, const uint32_t *hs, int all, uint32_t ms) {
    for (;;) {
        uint32_t r;
        if (emu_wait_poll(decomp_emu, n, hs, all, ms, &r)) return r;
        must_block("WaitFor...Object(s)");
        emu_wait_block_state(decomp_emu, ms);
        decomp_coro_block();
    }
}
uint32_t vc_WaitForSingleObject(uint32_t h, uint32_t ms) { return vc_WaitForMultipleObjects(1, &h, 0, ms); }
int32_t vc_PostMessageA(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    uint32_t a[4] = { hwnd, msg, wp, lp };
    return (int32_t)api(0x6369e1f8, 4, a);
}
int32_t vc_SetThreadPriority(uint32_t h, int32_t prio) { uint32_t a[2] = { h, (uint32_t)prio }; return (int32_t)api(0x6369e03c, 2, a); }
void vc_DebugBreak(void) { api(0x6369e084, 0, NULL); }
uint32_t vc_com_release(uint32_t obj) {
    uint32_t vt = *(uint32_t *)(void *)(decomp_guest_mem + obj);
    uint32_t fn = *(uint32_t *)(void *)(decomp_guest_mem + vt + 8);
    return emu_call(decomp_emu, fn, 1, &obj);
}
