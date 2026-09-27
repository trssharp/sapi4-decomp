// The rule procedures: what a rule's output 0xfe,n runs instead of a replacement string (fe_vm.c,
// vm_apply). Each instance has a table of them; most are the same thin wrapper that runs another rule
// set of the same machine over the segment (vm_main), the rest do tape work of their own.
#include "fe_vm.h"
#include <stddef.h>

enum { PROC_GUEST, PROC_MAIN, PROC_FALSE, PROC_FILL_FORWARD, PROC_CLEAR4, PROC_QUOTES, PROC_CHOOSE,
       PROC_NUMBER_DASHES, PROC_BLANK3, PROC_COPY_1_0, PROC_COPY_3_0, PROC_COPY_0_123 };
typedef struct RuleProc {
    uint32_t addr;          // the original (for PROC_GUEST: still called there)
    uint8_t kind;
    int8_t mode;            // PROC_MAIN: the rule set
    int8_t res;             // PROC_MAIN: -1 = its result, else this constant in AL
} RuleProc;
static const RuleProc rule_procs[3][62] = {
    { // instance A
        [3] = { 0x6368b28b, PROC_FILL_FORWARD, 0, 0 },
        [4] = { 0x6368b2e0, PROC_CLEAR4, 0, 0 },
        [5] = { 0x6368b6df, PROC_MAIN, 5, -1 },
        [6] = { 0x6368b6f5, PROC_MAIN, 6, 1 },
        [7] = { 0x6368b70d, PROC_MAIN, 7, 1 },
        [8] = { 0x6368b725, PROC_MAIN, 8, 1 },
        [9] = { 0x6368b73d, PROC_MAIN, 9, 1 },
        [10] = { 0x6368b755, PROC_MAIN, 10, -1 },
        [11] = { 0x6368b383, PROC_GUEST, 0, 0 },
        [12] = { 0x6368b305, PROC_QUOTES, 0, 0 },
        [13] = { 0x6368b6bc, PROC_CHOOSE, 0, 0 },
        [14] = { 0x6368b76b, PROC_MAIN, 14, 1 },
        [15] = { 0x6368b783, PROC_MAIN, 15, 1 },
        [16] = { 0x6368b79b, PROC_MAIN, 16, -1 },
    },
    { // instance B
        [3] = { 0x636908d3, PROC_MAIN, 3, -1 },
        [4] = { 0x6369048f, PROC_GUEST, 0, 0 },
        [5] = { 0x63690633, PROC_GUEST, 0, 0 },
        [6] = { 0x636908e9, PROC_MAIN, 6, 1 },
        [7] = { 0x63690215, PROC_GUEST, 0, 0 },
        [8] = { 0x6369024b, PROC_GUEST, 0, 0 },
        [9] = { 0x63690901, PROC_MAIN, 9, -1 },
        [10] = { 0x63690917, PROC_MAIN, 10, -1 },
        [11] = { 0x6369092d, PROC_MAIN, 11, 1 },
        [12] = { 0x63690945, PROC_MAIN, 12, -1 },
        [13] = { 0x6369095b, PROC_MAIN, 13, 1 },
        [14] = { 0x63690973, PROC_MAIN, 14, 1 },
        [15] = { 0x6369098b, PROC_MAIN, 15, -1 },
        [16] = { 0x636909a1, PROC_MAIN, 16, -1 },
    },
    { // instance C
        [3] = { 0x636939b0, PROC_NUMBER_DASHES, 0, 0 },
        [4] = { 0x63693984, PROC_BLANK3, 0, 0 },
        [5] = { 0x63693b64, PROC_MAIN, 5, -1 },
        [6] = { 0x63693b7a, PROC_MAIN, 6, -1 },
        [7] = { 0x63693b90, PROC_MAIN, 7, -1 },
        [8] = { 0x63693ba6, PROC_MAIN, 8, -1 },
        [9] = { 0x63693bbc, PROC_MAIN, 9, -1 },
        [10] = { 0x63693bd2, PROC_MAIN, 10, -1 },
        [11] = { 0x63693be8, PROC_MAIN, 11, -1 },
        [12] = { 0x63693bfe, PROC_MAIN, 12, -1 },
        [13] = { 0x63693c14, PROC_MAIN, 13, -1 },
        [14] = { 0x63693c2a, PROC_MAIN, 14, -1 },
        [15] = { 0x63693c40, PROC_MAIN, 15, -1 },
        [16] = { 0x63693c56, PROC_MAIN, 16, -1 },
        [17] = { 0x63693c6c, PROC_MAIN, 17, -1 },
        [18] = { 0x63693c82, PROC_MAIN, 18, -1 },
        [19] = { 0x63693c98, PROC_MAIN, 19, -1 },
        [20] = { 0x63693cae, PROC_MAIN, 20, -1 },
        [21] = { 0x6369a4ea, PROC_GUEST, 0, 0 },
        [22] = { 0x6369986b, PROC_GUEST, 0, 0 },
        [23] = { 0x636938b9, PROC_COPY_1_0, 0, 0 },
        [24] = { 0x63693cc4, PROC_MAIN, 24, 0 },
        [25] = { 0x636993f8, PROC_GUEST, 0, 0 },
        [26] = { 0x63698f1a, PROC_GUEST, 0, 0 },
        [27] = { 0x63698a3e, PROC_GUEST, 0, 0 },
        [28] = { 0x63693cdc, PROC_MAIN, 28, -1 },
        [29] = { 0x63693cf2, PROC_MAIN, 29, -1 },
        [30] = { 0x6369382c, PROC_GUEST, 0, 0 },
        [31] = { 0x63693925, PROC_COPY_0_123, 0, 0 },
        [32] = { 0x63693d08, PROC_MAIN, 32, -1 },
        [33] = { 0x63693d1e, PROC_MAIN, 33, -1 },
        [34] = { 0x63698594, PROC_GUEST, 0, 0 },
        [35] = { 0x636980bf, PROC_GUEST, 0, 0 },
        [36] = { 0x63697c17, PROC_GUEST, 0, 0 },
        [37] = { 0x63693d34, PROC_MAIN, 37, -1 },
        [38] = { 0x63693d4a, PROC_MAIN, 38, -1 },
        [39] = { 0x63693d60, PROC_MAIN, 39, -1 },
        [40] = { 0x63693d76, PROC_MAIN, 40, -1 },
        [41] = { 0x6369397f, PROC_FALSE, 0, 0 },
        [42] = { 0x63693a03, PROC_GUEST, 0, 0 },
        [43] = { 0x63693d8c, PROC_MAIN, 43, -1 },
        [44] = { 0x63693da2, PROC_MAIN, 44, 0 },
        [45] = { 0x63693dba, PROC_MAIN, 45, 0 },
        [46] = { 0x63693dd2, PROC_MAIN, 46, -1 },
        [47] = { 0x63693de8, PROC_MAIN, 47, -1 },
        [48] = { 0x63693dfe, PROC_MAIN, 48, 1 },
        [49] = { 0x63693e16, PROC_MAIN, 49, 0 },
        [50] = { 0x63693e2e, PROC_MAIN, 50, -1 },
        [51] = { 0x63693e44, PROC_MAIN, 51, -1 },
        [52] = { 0x636938ef, PROC_COPY_3_0, 0, 0 },
        [53] = { 0x6369773d, PROC_GUEST, 0, 0 },
        [54] = { 0x636972c7, PROC_GUEST, 0, 0 },
        [55] = { 0x63693e5a, PROC_MAIN, 55, -1 },
        [56] = { 0x63693e70, PROC_MAIN, 56, -1 },
        [57] = { 0x63693e86, PROC_MAIN, 57, -1 },
        [58] = { 0x63693e9c, PROC_MAIN, 58, 1 },
        [59] = { 0x63693eb4, PROC_MAIN, 59, 1 },
        [60] = { 0x63693ecc, PROC_MAIN, 60, -1 },
        [61] = { 0x63693ee2, PROC_MAIN, 61, -1 },
    },
};

#define T(k) GP(char, *DLLVAR(GPTR(char), rule_vm[inst].tape[k]))

uint32_t vm_proc(int inst, uint8_t n, int16_t *left, int16_t *len, uint32_t prev_eax) {
    const RuleProc *p = n < 62 ? &rule_procs[inst][n] : NULL;
    uint32_t al1 = (prev_eax & ~0xffu) | 1u;
    switch (p ? p->kind : PROC_GUEST) {
    case PROC_MAIN: {
        // (the original passes *left with the upper half of the pointer to it; only the low half is used)
        uint32_t e = vm_main(inst, p->mode, *left, len);
        return p->res < 0 ? e : (e & ~0xffu) | (uint8_t)p->res;
    }
    case PROC_FALSE:
        return prev_eax & ~0xffu;
    case PROC_CHOOSE:                       // A, 13: one of two rule sets, by a global flag
        return (uint8_t)vm_proc(inst, *DLLVAR(int32_t, 0x63738c98) ? 15 : 14, left, len, prev_eax);
    case PROC_FILL_FORWARD: {               // A, 3: tape 2: fill filler with the last real character
        char fill = 0x7e;
        int32_t last = *left;
        for (int32_t i = *left; i <= *len; i++) {
            char c = T(2)[i - 1];
            if (c != 0x7e) { fill = c; last = i; }
            else T(2)[i - 1] = fill;
        }
        for (int32_t j = *left - 1; j <= last - 2; j++) T(2)[j] = fill;
        return al1;
    }
    case PROC_CLEAR4:                       // A, 4: tape 4 of the segment becomes filler
        for (int32_t i = *left - 1; i < *len; i++) T(4)[i] = 0x7e;
        return al1;
    case PROC_QUOTES:                       // A, 12: move tape 3's mark of a quote / G over following filler
        for (int16_t i = *left; i < *len; i++) {
            uint8_t c = (uint8_t)T(0)[i - 1];
            if (c != 0x22 && c != 0x27 && c != 0x60 && c != 0x47) continue;
            char d = T(3)[i - 1];
            if (d == 0x7e) continue;
            int16_t k = 1;
            if (T(0)[i] == 0x7e)
                do k++; while (T(0)[k + i - 1] == 0x7e);
            T(3)[k + i - 1] = d;
        }
        return al1;
    case PROC_NUMBER_DASHES: {              // C, 3: number the dashes of tape 0 from the end on tape 3
        int16_t k = 1;
        for (int16_t i = (int16_t)(*len - 1); i >= *left - 1; i--) {
            if (T(0)[i] != 0x2d) continue;
            if (k < 10) T(3)[i] = (char)(k + 0x30);
            k++;
        }
        return al1;
    }
    case PROC_BLANK3:                       // C, 4
        for (int16_t i = (int16_t)(*left - 1); i < *len; i++) T(3)[i] = 0x20;
        return al1;
    case PROC_COPY_1_0:                     // C, 23
        for (int16_t i = (int16_t)(*left - 1); i < *len; i++) T(0)[i] = T(1)[i];
        return al1;
    case PROC_COPY_3_0:                     // C, 52
        for (int16_t i = (int16_t)(*left - 1); i < *len; i++) T(0)[i] = T(3)[i];
        return al1;
    case PROC_COPY_0_123:                   // C, 31
        for (int16_t i = (int16_t)(*left - 1); i < *len; i++) {
            T(1)[i] = T(0)[i];
            T(2)[i] = T(0)[i];
            T(3)[i] = T(0)[i];
        }
        return al1;
    default:
        return (prev_eax & ~0xffu) | vm_proc_guest(inst, n, left, len);
    }
}

uint8_t vm_callback(int inst, uint8_t n, int16_t *left, int16_t *len) {
    return (uint8_t)vm_proc(inst, n, left, len, 0);
}

int vm_proc_is_c(int inst, uint8_t n) { return n < 62 && rule_procs[inst][n].kind != PROC_GUEST; }
