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

const RunVM run_vm[3] = {
    { 0x63738ce8, 0x637392e0, 0x6373924c, 0x63738cb8, 0x63738cd0, 0x63738cbc, 0x63739250, 0x63739340,
      0x63739288, 0x637392e8, 0x63739220, 0x63739370, 0x6373927c, 0x6373939c, 0x63739200, 0x63721ccc, 0x63738ce0, 8 },
    { 0x637393cc, 0x637394a8, 0x63739424, 0x637393ac, 0x637393b8, 0x637393b0, 0x63739428, 0x63739508,
      0x63739450, 0x637394b0, 0x637393f8, 0x63739538, 0x63739448, 0x63739564, 0x637393d8, 0x63721f2c, 0x637393c4, 7 },
    { 0x637395b0, 0x63739688, 0x63739604, 0x63739574, 0x63739580, 0x63739578, 0x63739608, 0x637396e8,
      0x63739630, 0x63739690, 0x637395d8, 0x63739718, 0x63739628, 0x63739744, 0x637395bc, 0x63721f7c, 0x63739590, 30 },
};

uint8_t vm_run(int inst) {
    const RunVM *vm = &run_vm[inst];
    int16_t *step = DLLVAR(int16_t, vm->step), *nch = DLLVAR(int16_t, vm->nch);
    int16_t *pos0 = DLLVAR(int16_t, vm->pos0), *pos = DLLVAR(int16_t, vm->pos);
    GPTR(uint8_t) *pc = DLLVAR(GPTR(uint8_t), vm->pc);
#define PCP GP(uint8_t, *pc)
    int16_t s = *step;
    GPSET(*pc, PCP + 1);
    *nch = 0;
    uint8_t ok = 1;
    *pos0 = s;
    *DLLVAR(uint8_t, vm->flag) = 1;
run:        // (the original has four copies of this loop; they behave the same)
    while (*PCP != 0xff) {
        if (!ok) goto backtrack;
        ok = vm_exec(inst);
        GPSET(*pc, PCP + 1);
        *pos = (int16_t)(*pos + *step);
    }
check:
    if (ok) return ok;
backtrack: {
        int16_t n = *nch;
        if (n <= 0) return ok;      // (ok is 0 here)
        uint8_t t = ++DLLVAR(uint8_t, vm->tries)[n];
        *pos = DLLVAR(int16_t, vm->cpos)[n];
        const uint8_t *at = GP(const uint8_t, DLLVAR(GPTR(uint8_t), vm->cpc)[n]);
        *pos0 = DLLVAR(int16_t, vm->cpos0)[n];
        *pc = DLLVAR(GPTR(uint8_t), vm->cpc)[n];
        *DLLVAR(GPTR(char), vm->cur) = DLLVAR(GPTR(char), vm->ctape)[n];
        switch (DLLVAR(int16_t, vm->ckind)[n]) {
        case 0xf5:
            (*nch)--;
            ok = 1;
            goto run;
        case 0xf6:
            if (!(uint8_t)vm_skip_block(&rule_vm[inst], 0xf6, t)) {
                GPSET(*pc, PCP + 2);
                (*nch)--;
                goto check;
            }
            GPSET(*pc, PCP + 2);
            ok = 1;
            goto run;
        case 0xf7:
            if (t > at[2]) {
                (*nch)--;
                goto check;
            }
            *DLLVAR(uint8_t, vm->flag) = 0;
            ok = 1;
            goto run;
        default:
            goto check;             // (the same choice point again: never happens in the rules)
        }
    }
#undef PCP
}

const MatchVM match_vm[3] = {
    { 0x63738cb8, 0x636a1d80, 0x63738cb0, 0x6373926c, 0x63738cac, 0x637391fc, 0x63738cb4 },
    { 0x637393ac, 0x636a5000, 0x637393a4, 0x63739440, 0x637393a0, 0x637393d4, 0x637393a8 },
    { 0x63739574, 0x636ab818, 0x6373956c, 0x63739620, 0x63739568, 0x637395b8, 0x63739570 },
};

// one class / literal test as the repetition opcode 0xed encodes it (the same tests as 0xf2..0xfc)
static int match_test(const CondVM *cv, uint8_t op, uint8_t arg, uint8_t ch) {
    switch (op) {
    case 0xf2: return ch != arg;
    case 0xf3: return ch == arg;
    case 0xf9: return DLLVAR(const uint8_t, cv->t1)[arg * 0xa2 + ch] == 0;
    case 0xfa: return DLLVAR(const uint8_t, cv->t1)[arg * 0xa2 + ch] != 0;
    case 0xfb: return DLLVAR(const uint8_t, cv->t2)[arg * 0xa2 + ch] == 0;
    case 0xfc: return DLLVAR(const uint8_t, cv->t2)[arg * 0xa2 + ch] != 0;
    }
    return 0;
}

int16_t vm_match(int inst, int32_t at, int32_t back) {
    const CondVM *cv = &cond_vm[inst];
    const MatchVM *mv = &match_vm[inst];
    GPTR(uint8_t) *PCv = DLLVAR(GPTR(uint8_t), cv->pc);
    int16_t *adv = DLLVAR(int16_t, mv->adv);
    uint8_t *chg = DLLVAR(uint8_t, cv->ch);
    int16_t *last = DLLVAR(int16_t, mv->last);
    int16_t pos = (int16_t)at;
    uint32_t cnt = 0;                       // one counter for the whole call, as in the original
    uint8_t *pc = GP(uint8_t, *PCv);
#define TP() GP(char, *DLLVAR(GPTR(char), cv->cur))
#define AT(i) ((uint8_t)TP()[(int16_t)(i)])
#define SKIP(step) do { if (*adv) while (AT(pos) == 0x7e) pos = (int16_t)(pos + (step)); } while (0)
#define READ() (*chg = AT(pos))
#define FAIL return 0
    for (;;) {
        uint8_t op = pc[0];
        switch (op) {
        case 0xff:
            return pos;
        case 0xfd: {                        // variable: bind (checking its class) or compare
            SKIP(1);
            uint8_t ch = READ(), v = pc[1];
            uint8_t b = DLLVAR(uint8_t, cv->vars)[v];
            if (b == 0) {
                const uint8_t *cl = DLLVAR(const uint8_t, cv->cls) + v * 2;
                if (cl[0] == 4) {
                    if (!DLLVAR(const uint8_t, cv->t4)[cl[1] * 0xa2 + ch]) FAIL;
                } else if (cl[0] == 5) {
                    if (!DLLVAR(const uint8_t, cv->t3)[cl[1] * 0xa2 + ch]) FAIL;
                }
                *DLLVAR(int16_t, cv->bound) = 1;
                DLLVAR(uint8_t, cv->vars)[v] = ch;
            } else if (ch != b) FAIL;
            goto next2;
        }
        case 0xea: {                        // not the (bound) variable
            SKIP(1);
            uint8_t ch = READ();
            uint8_t b = DLLVAR(uint8_t, cv->vars)[pc[1]];
            if (b != 0 && ch == b) FAIL;
            goto next2;
        }
        case 0xf2: case 0xf3: case 0xf9: case 0xfa: case 0xfb: case 0xfc: {
            SKIP(1);
            uint8_t ch = READ();
            if (ch == 0x0e || !match_test(cv, op, pc[1], ch)) FAIL;
            goto next2;
        }
        case 0xf8: {                        // class member: also yields its value
            SKIP(*adv);
            pc++;
            GPSET(*PCv, pc);
            uint8_t ch = READ();
            uint32_t k = pc[0] * 0xa2u + ch;
            if (DLLVAR(const uint8_t, cv->t1)[k] != 1) FAIL;
            *DLLVAR(uint8_t, mv->out) = DLLVAR(const uint8_t, mv->valtab)[k];
            goto next1;
        }
        case 0xf7: {                        // embedded condition up to 0xf7
            SKIP(*adv);
            READ();
            pc++;
            GPSET(*PCv, pc);
            if (AT(pos) == 0x0e) FAIL;
            if (!(uint8_t)vm_cond(cv, 0xf7, pos)) FAIL;
            pc = GP(uint8_t, *PCv);
            pos++;
            *adv = 1;
            pc++;
            GPSET(*PCv, pc);
            continue;
        }
        case 0xf4: {                        // repeated condition: min pc[1], max pc[2] (0xdc: 2200)
            *last = (int16_t)(pos - 1);
            SKIP(*adv);
            READ();
            uint8_t *start = pc;
            int16_t n = start[2] == 0xdc ? 0x898 : start[2];
            *DLLVAR(int16_t, mv->maxcnt) = n;
            cnt = 0;
            if (n > 0) {
                pc = start + 3;
                for (;;) {
                    GPSET(*PCv, pc);
                    if (AT(pos) == 0x0e) break;
                    if (!(uint8_t)vm_cond(cv, 0xf4, pos)) { pc = GP(uint8_t, *PCv); break; }
                    *last = pos;
                    pos++;
                    while (AT(pos) == 0x7e) pos++;
                    cnt++;
                    READ();
                    if (!((int16_t)cnt < *DLLVAR(int16_t, mv->maxcnt))) { pc = GP(uint8_t, *PCv); break; }
                }
            }
            if ((int16_t)cnt < (int16_t)start[1]) FAIL;
            while (*pc != 0xf4) { pc++; GPSET(*PCv, pc); }
            *adv = 1;
            pos = (int16_t)(*last + 1);
            goto next1;
        }
        case 0xed: {                        // repeated test: min pc[1], max pc[2], test pc[3] with pc[4]
            *last = (int16_t)(pos - 1);
            SKIP(1);
            READ();
            int16_t n = pc[2] == 0xdc ? 0x898 : pc[2];
            *DLLVAR(int16_t, mv->maxcnt) = n;
            uint8_t t = pc[3], arg = pc[4];
            *DLLVAR(uint8_t, mv->clsb) = arg;
            if (t == 0xf2 || t == 0xf3 || (t >= 0xf9 && t <= 0xfc)) {
                cnt = 0;
                if (n > 0) {
                    for (;;) {
                        uint8_t ch = READ();
                        if (ch == 0x0e || !match_test(cv, t, arg, ch)) break;
                        *last = pos;
                        do pos++; while (AT(pos) == 0x7e);
                        cnt++;
                        if (!((int16_t)cnt < n)) break;
                    }
                }
            }
            if ((int16_t)cnt < (int16_t)pc[1]) FAIL;
            *adv = 1;
            pc += 6;
            pos = (int16_t)(*last + 1);
            GPSET(*PCv, pc);
            continue;
        }
        case 0xef: case 0xf1:               // switch to tape pc[1]
            *DLLVAR(GPTR(char), mv->saved) = *DLLVAR(GPTR(char), cv->cur);
            *DLLVAR(GPTR(char), cv->cur) = DLLVAR(GPTR(char), cv->tapes)[pc[1]];
            *adv = 0;
            goto next2;
        case 0xee: case 0xf0:               // back to the previous tape
            *DLLVAR(GPTR(char), cv->cur) = *DLLVAR(GPTR(char), mv->saved);
            *adv = 0;
            goto next2;
        case 0xeb:                          // continue after `back`
            *adv = 1;
            pos = (int16_t)(back + 1);
            pc++;
            GPSET(*PCv, pc);
            continue;
        case 0xec:                          // skip filler
            while (AT(pos) == 0x7e) pos++;
            READ();
            *adv = 1;
            pc++;
            GPSET(*PCv, pc);
            continue;
        default: {                          // a literal character (also 0xf5, 0xf6, 0xfe)
            SKIP(1);
            if (READ() != op) FAIL;
            goto next1;
        }
        }
    next2:
        if (op != 0xef && op != 0xf1 && op != 0xee && op != 0xf0) { *adv = 1; pos++; }
        pc += 2;
        GPSET(*PCv, pc);
        continue;
    next1:
        if (op != 0xf4) { *adv = 1; pos++; }
        pc++;
        GPSET(*PCv, pc);
    }
#undef TP
#undef AT
#undef SKIP
#undef READ
#undef FAIL
}

LAYOUT(RuleHdr, tape, 0x294);

uint32_t vm_try(int inst, int32_t a0, int32_t a1, RuleHdr *r, uint32_t prev_eax) {
    const RunVM *vm = &run_vm[inst];
    uint8_t ok = 0;
    if (inst == 0) {
        if (*DLLVAR(int16_t, rule_vm[0].top) - r->len < 2) {
            *DLLVAR(int32_t, vm->abort) = 1;
            r->applied = 0;
            return (prev_eax & 0xffff0000u) | ((uint16_t)r->len & 0xff00u);
        }
    } else if (*DLLVAR(int32_t, vm->abort) != 0) {
        return prev_eax & ~0xffu;
    }
    uint32_t eax = (prev_eax & 0xffff0000u) | (uint16_t)r->len;
    int16_t e = vm_match(inst, a1, (int32_t)eax);
    if (e != 0) {
        *DLLVAR(int16_t, vm->pos2) = (int16_t)(e - r->left);
        *DLLVAR(int16_t, vm->step) = -1;
        *DLLVAR(int16_t, vm->pos) = (int16_t)(r->left - 1);
        if (vm_run(inst)) {
            *DLLVAR(int16_t, vm->step) = 1;
            *DLLVAR(int16_t, vm->pos) = e;
            if (vm_run(inst) && vm_apply(inst, (int16_t)a0, r)) {
                r->applied = 1;
                ok = 1;
            }
        }
    }
    if (*DLLVAR(int16_t, vm->bound) != 0) {
        *DLLVAR(int16_t, vm->bound) = 0;
        uint8_t *v = DLLVAR(uint8_t, vm->vars);
        for (uint32_t i = 0; i < vm->nvars; i++) v[i] = 0;
    }
    *DLLVAR(GPTR(char), vm->cur) = r->tape;
    return ((uint32_t)GRAW(r->tape) & ~0xffu) | ok;
}
