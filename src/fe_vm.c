#include "fe_vm.h"

const RuleVM rule_vm[3] = {
    { { 0x63739268, 0x63739278, 0x63738ca8, 0x63739204, 0x637391f8, 0x63738cf0 }, 0x63721cc8, 0x637392e0 },
    { { 0x63739e7c, 0x63739e6c, 0x63739e78, 0x63739e70, 0x63739e74, 0x637393d0 }, 0x63721f28, 0x637394a8 },
    { { 0x63739e7c, 0x63739e6c, 0x63739e78, 0x63739e70, 0x63739e74, 0x637395b4 }, 0x63721f78, 0x63739688 },
};

#define TAPE(vm, k) GP(char, *DLLVAR(GPTR(char), (vm)->tape[k]))
#define TOP(vm) (*DLLVAR(int16_t, (vm)->top))
#define PC(vm) (*DLLVAR(GPTR(uint8_t), (vm)->pc))

void vm_shift_right(const RuleVM *vm, int16_t lo, int16_t hi) {
    if (hi >= lo)
        for (int32_t i = hi, n = hi - lo + 1; n; n--, i--)
            for (int k = 0; k < 6; k++) TAPE(vm, k)[i] = TAPE(vm, k)[i - 1];
    for (int k = 0; k < 5; k++) {
        TAPE(vm, k)[hi + 1] = 0x0e;
        TAPE(vm, k)[lo - 1] = 0x0e;
    }
    TAPE(vm, 5)[hi + 1] = 0;
    TAPE(vm, 5)[lo - 1] = 0;
}

uint32_t vm_shift_left(const RuleVM *vm, int16_t lo, int16_t hi, uint32_t prev_eax) {
    if (lo > hi) return (prev_eax & 0xffff0000u) | (uint16_t)lo;
    int32_t i = lo;
    for (int32_t n = hi - lo + 1; n; n--, i++)
        for (int k = 0; k < 6; k++) TAPE(vm, k)[i - 2] = TAPE(vm, k)[i - 1];
    return (uint32_t)i;
}

void vm_pop_range(const RuleVM *vm, int16_t a, int16_t b) {
    int32_t stop = a - 1;
    for (int32_t i = b; i >= stop; b--, i = b) {
        TOP(vm)--;
        for (int k = 0; k < 6; k++) TAPE(vm, k)[TOP(vm) - 1] = TAPE(vm, k)[i];
        TAPE(vm, 5)[i] = 0;
    }
}

int32_t vm_push_until(const RuleVM *vm, int32_t s) {
    do {
        s++;
        int16_t i = (int16_t)s;
        for (int k = 0; k < 6; k++) TAPE(vm, k)[i - 1] = TAPE(vm, k)[TOP(vm) - 1];
        TOP(vm)++;
    } while (TAPE(vm, 0)[(int16_t)s - 1] != 0x0e);
    return s - 1;
}

uint32_t vm_skip_block(const RuleVM *vm, uint8_t tok, uint8_t end) {
    uint8_t *p = GP(uint8_t, PC(vm));
    uint8_t d = p[0], c = p[1];
    for (;;) {
        if (d == tok && c == end) return ((uint32_t)(uintptr_t)GRAW(PC(vm)) & ~0xffu) | 1u;
        if ((d == tok && c == 1) || (tok == 0xf5 && d == tok && c != 0)) {
            GPSET(PC(vm), p + 1);
            vm_skip_block(vm, tok, 0);
            p = GP(uint8_t, PC(vm)) + 2;
        }
        p++;
        GPSET(PC(vm), p);
        c = p[0];
        if (c == tok && p[1] == 0) return ((uint32_t)GRAW(PC(vm)) & ~0xffu);
        d = c;
        c = p[1];
    }
}

int32_t vm_move(const RuleVM *vm, int32_t a, int32_t b, int32_t d) {
    if ((int16_t)d > 0) {
        int32_t stop = (int16_t)a - 1;
        int32_t i = (int16_t)b;
        if (i >= stop) {
            int32_t dst = d + b + 1, cnt = b;
            do {
                int16_t k0 = (int16_t)dst;
                for (int k = 0; k < 6; k++) TAPE(vm, k)[k0 - 1] = TAPE(vm, k)[i];
                TAPE(vm, 5)[i] = 0;
                dst--;
                cnt--;
                i = (int16_t)cnt;
            } while (i >= stop);
        }
    } else {
        int32_t start = a - 1;
        if (!((int16_t)start > (int16_t)b)) {
            int32_t n = (int16_t)b - (int16_t)start + 1;
            int32_t i = (int16_t)start, dst = start + d + 1;
            for (; n; n--, dst++, i++) {
                int16_t k0 = (int16_t)dst;
                for (int k = 0; k < 5; k++) TAPE(vm, k)[k0 - 1] = TAPE(vm, k)[i];
                TAPE(vm, 5)[k0 - 1] = (char)(TAPE(vm, 5)[k0 - 1] + TAPE(vm, 5)[i]);
                TAPE(vm, 5)[i] = 0;
            }
        }
    }
    return b + d;
}

const CondVM cond_vm[3] = {
    { 0x637392e0, 0x63739284, 0x6373927c, 0x6373933c, 0x63739204, 0x636a1cd8, 0x636a0750, 0x63738ce0,
      0x636a1ed6, 0x636a1d7a, 0x636a07f2, 0x63721ccc, 1 },
    { 0x637394a8, 0x6373944c, 0x63739448, 0x63739504, 0x637393dc, 0x636a4eb8, 0x636a3238, 0x637393c4,
      0x636a522e, 0x636a4f5a, 0x636a32da, 0x63721f2c, 0 },
    { 0x63739688, 0x6373962c, 0x63739628, 0x637396e4, 0x637395bc, 0x636ab308, 0x636a7c98, 0x63739590,
      0x636abea6, 0x636ab3aa, 0x636a7d3a, 0x63721f7c, 0 },
};

uint32_t vm_cond(const CondVM *vm, uint8_t end, int32_t pos) {
    GPTR(uint8_t) *PCv = DLLVAR(GPTR(uint8_t), vm->pc);
    uint8_t *pc = GP(uint8_t, *PCv);
    if (vm->pos16) pos = (int16_t)pos;
#define RET(r) return ((uint32_t)GRAW(*PCv) & ~0xffu) | (r)   // (pc == *PCv at every return)
    if (pc[0] == end) RET(1u);
    GPTR(char) saved = *DLLVAR(GPTR(char), vm->saved);
    GPTR(char) cur = *DLLVAR(GPTR(char), vm->cur);
    uint8_t ch = *DLLVAR(uint8_t, vm->ch);
    const uint8_t *vars = DLLVAR(const uint8_t, vm->vars);
    for (;;) {
        uint8_t op = pc[0], arg = pc[1];
        switch (op) {
        case 0xef: case 0xf1:
            saved = cur;
            *DLLVAR(GPTR(char), vm->saved) = saved;
            cur = DLLVAR(GPTR(char), vm->tapes)[arg];
            *DLLVAR(GPTR(char), vm->cur) = cur;
            ch = (uint8_t)GP(char, cur)[pos];
            *DLLVAR(uint8_t, vm->ch) = ch;
            break;
        case 0xee: case 0xf0:
            cur = saved;
            *DLLVAR(GPTR(char), vm->cur) = cur;
            ch = (uint8_t)GP(char, saved)[pos];
            *DLLVAR(uint8_t, vm->ch) = ch;
            break;
        case 0xf2: if (arg == ch) RET(0u); break;
        case 0xf3: if (arg != ch) RET(0u); break;
        case 0xf9: if (DLLVAR(const uint8_t, vm->t1)[arg * 0xa2 + ch]) RET(0u); break;
        case 0xfa: if (!DLLVAR(const uint8_t, vm->t1)[arg * 0xa2 + ch]) RET(0u); break;
        case 0xfb: if (DLLVAR(const uint8_t, vm->t2)[arg * 0xa2 + ch]) RET(0u); break;
        case 0xfc: if (!DLLVAR(const uint8_t, vm->t2)[arg * 0xa2 + ch]) RET(0u); break;
        case 0xfd: {
            uint8_t v = vars[arg];
            if (v) {
                if (ch != v) RET(0u);
                break;
            }
            const uint8_t *cl = DLLVAR(const uint8_t, vm->cls) + arg * 2;
            if (cl[0] == 4) {
                if (!DLLVAR(const uint8_t, vm->t4)[cl[1] * 0xa2 + ch]) RET(0u);
            } else if (cl[0] == 5) {
                if (!DLLVAR(const uint8_t, vm->t3)[cl[1] * 0xa2 + ch]) RET(0u);
            }
            *DLLVAR(int16_t, vm->bound) = 1;
            DLLVAR(uint8_t, vm->vars)[arg] = ch;
            break;
        }
        case 0xea: {
            uint8_t v = vars[arg];
            if (v && ch == v) RET(0u);
            break;
        }
        default:
            for (;;) {}   // (the original spins here; see the header)
        }
        pc += 2;
        GPSET(*PCv, pc);
        if (pc[0] == end) RET(1u);
    }
#undef RET
}
