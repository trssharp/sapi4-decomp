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
