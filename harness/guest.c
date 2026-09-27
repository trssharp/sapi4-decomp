// Calls from decompiled C into functions of the original that are not decompiled yet. Each stub
// marshals its arguments to the guest (pointers to C locals are copied onto the guest stack, below the
// hooked function's frame) and runs the original with emu_call. A portable build has no stubs: a
// function listed here must be decompiled before it links.
#include "hooks.h"
#include "unitsel.h"
#include <string.h>

static uint32_t guest_call(uint32_t pref, int nargs, const uint32_t *args) {
    return emu_call(decomp_emu, pref + decomp_load_delta, nargs, args);
}
static uint32_t host_to_guest_ptr(const void *p) { return p ? (uint32_t)((const uint8_t *)p - decomp_guest_mem) : 0; }

int32_t tree_query(const SenoneTree *t, TreeQuery *q) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - (uint32_t)sizeof *q) & ~15u;
    uint32_t gq = c->s.r[ESP];
    memcpy(decomp_guest_mem + gq, q, sizeof *q);
    uint32_t args[2] = { host_to_guest_ptr(t), gq };
    int32_t r = (int32_t)guest_call(0x6367233c, 2, args);
    memcpy(q, decomp_guest_mem + gq, sizeof *q);
    c->s.r[ESP] = sp;
    return r;
}

static uint32_t guest_thiscall(uint32_t pref, const void *self, int nargs, const uint32_t *args) {
    emu_cpu(decomp_emu)->s.r[ECX] = host_to_guest_ptr(self);
    return guest_call(pref, nargs, args);
}
void unit_prosody_a(UnitStage *u, int n) { uint32_t a[1] = { (uint32_t)n }; guest_thiscall(0x63686c7d, u, 1, a); }
void unit_prosody_b(UnitStage *u, int n) { uint32_t a[1] = { (uint32_t)n }; guest_thiscall(0x63686905, u, 1, a); }
