// Calls from decompiled C into functions of the original that are not decompiled yet. Each stub
// marshals its arguments to the guest (pointers to C locals are copied onto the guest stack, below the
// hooked function's frame) and runs the original with emu_call. A portable build has no stubs: a
// function listed here must be decompiled before it links.
#include "hooks.h"
#include "unitsel.h"
#include "containers.h"
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

__attribute__((unused)) static uint32_t guest_thiscall(uint32_t pref, const void *self, int nargs, const uint32_t *args) {
    emu_cpu(decomp_emu)->s.r[ECX] = host_to_guest_ptr(self);
    return guest_call(pref, nargs, args);
}

int32_t unit_lattice_apply(UnitStage *u, GPTR(PhoneIn) *ph, int32_t *n, int32_t m, VecArray *va, uint8_t *bounds) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - 16u) & ~15u;
    uint32_t g = c->s.r[ESP];
    memcpy(decomp_guest_mem + g, ph, 4);
    memcpy(decomp_guest_mem + g + 4, n, 4);
    uint32_t args[5] = { g, g + 4, (uint32_t)m, host_to_guest_ptr(va), host_to_guest_ptr(bounds) };
    c->s.r[ECX] = host_to_guest_ptr(u);
    int32_t r = (int32_t)guest_call(0x63685d72, 5, args);
    memcpy(ph, decomp_guest_mem + g, 4);
    memcpy(n, decomp_guest_mem + g + 4, 4);
    c->s.r[ESP] = sp;
    return r;
}

double lattice_cost_other(uint32_t fn, int32_t a, int32_t b) {
    uint32_t args[2] = { (uint32_t)a, (uint32_t)b };
    emu_call(decomp_emu, fn, 2, args);
    return x86_fpu_pop(emu_cpu(decomp_emu));
}
