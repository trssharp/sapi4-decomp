#include "hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t decomp_load_delta;
_Thread_local uint8_t *decomp_guest_mem;
_Thread_local Emu *decomp_emu;

// MSVCRT rand() on the calling guest thread's own state (exactly what the emulator's CRT does)
int vc_rand(void) {
    Thread *t = decomp_emu->cur;
    t->rand_seed = t->rand_seed * 214013u + 2531011u;
    return (int)((t->rand_seed >> 16) & 0x7FFF);
}

static int selected(const char *list, const char *name) {
    if (!list || !*list || !strcmp(list, "all")) return 1;
    if (!strcmp(list, "none")) return 0;
    int excl_mode = list[0] == '-';
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", list);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        if (*tok == '-') tok++;
        if (!strcmp(tok, name)) return !excl_mode;
    }
    return excl_mode;
}

int decomp_install_hooks(Emu *e, uint32_t load_base, uint32_t pref_base) {
    const char *list = NULL;
#ifndef CV_NO_DEBUG_ENV
    list = getenv("SAPI4_HOOKS");
#endif
    decomp_load_delta = load_base - pref_base;
    decomp_guest_mem = emu_cpu(e)->mem;
    decomp_emu = e;
    int n = 0;
    for (int i = 0; i < decomp_nhooks; i++) {
        const DecompHook *h = &decomp_hooks[i];
        if (!selected(list, h->name)) continue;
        uint32_t a = h->addr - pref_base + load_base;
        uint32_t thunk = emu_thunk(e, h->name, h->fn, h->argbytes);
        uint32_t idx = (thunk - EMU_THUNK_BASE) / 4;
        uint8_t *p = emu_ptr(e, a, 3);
        p[0] = 0xF1; p[1] = (uint8_t)idx; p[2] = (uint8_t)(idx >> 8);
        n++;
    }
#ifndef CV_NO_DEBUG_ENV
    if (getenv("SAPI4_VERBOSE")) fprintf(stderr, "decomp: %d of %d hooks installed\n", n, decomp_nhooks);
#endif
    return n;
}

void decomp_report(Emu *e) {
#ifndef CV_NO_DEBUG_ENV
    if (!getenv("SAPI4_HOOK_STATS")) return;
    for (int i = 0; i < e->nhosts; i++)
        for (int k = 0; k < decomp_nhooks; k++)
            if (e->hosts[i].fn == decomp_hooks[k].fn)
                fprintf(stderr, "hook %08x %-28s %llu calls\n", decomp_hooks[k].addr, decomp_hooks[k].name,
                        (unsigned long long)e->hosts[i].calls);
#else
    (void)e;
#endif
}
