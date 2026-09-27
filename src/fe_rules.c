#include "fe_rules.h"

#define TAPE(addr) GP(char, *DLLVAR(GPTR(char), addr))

int tapes_graphic(int16_t lo, int16_t hi) {
    for (int16_t i = lo; i <= hi; i++) {
        uint8_t ch = (uint8_t)TAPE(TAPE_A)[i];
        if (ch < 0x21 || ch > 0xfe) return 0;
    }
    return 1;
}

int16_t tapes_pad(const int16_t *ends, int16_t a) {
    int16_t m = a;
    for (int k = 0; k < 4; k++)
        if (m < ends[k]) m = ends[k];
    static const uint32_t tapes[4] = { TAPE_A, TAPE_B, TAPE_C, TAPE_D };
    for (int k = 0; k < 4; k++)
        for (int16_t i = (int16_t)(ends[k] + 1); i <= m; i++) TAPE(tapes[k])[i] = '~';
    return m;
}
