#include "fe_lex.h"

uint32_t lex_pack(uint8_t bits, uint16_t count) {
    const uint8_t *codes = DLLVAR(const uint8_t, LEX_CODES);
    uint8_t *key = DLLVAR(uint8_t, LEX_KEY);
    uint32_t acc = 0;
    int16_t out = 0, i = 0;
    uint8_t used = bits;                    // (bits in the window after the next code, plus `bits`)
    do {
        used = (uint8_t)(used + bits);
        acc += codes[i++];
        uint8_t sh = bits;
        if ((int8_t)used >= 16) {
            sh = (uint8_t)(bits - used + 16);
            acc = (acc << (sh & 31)) & 0xffff;
            key[out++] = (uint8_t)(acc >> 8);
            uint8_t rest = (uint8_t)(bits - sh);
            acc = (acc << (rest & 31)) & 0xffff;
            used = (uint8_t)(rest + 8);
        } else {
            acc = (acc << (sh & 31)) & 0xffff;
        }
    } while (--count != 0);
    acc = (acc << ((uint8_t)(16 - used) & 31)) & 0xffff;
    key[out] = (uint8_t)(acc >> 8);
    key[out + 1] = (uint8_t)acc;
    return acc;
}

uint32_t lex_field(uint32_t table, uint16_t idx, int n) {
    const uint16_t *t = DLLVAR(const uint16_t, table);
    uint32_t bit = (uint32_t)idx * (uint32_t)n;
    uint32_t w = (bit >> 4) & 0xffff, b = bit & 15;
    if (b + (uint32_t)n > 16)   // (the original adds the second word in a register that held 2 * w)
        return ((t[w] & (0xffffu >> b)) << (b - (16 - n))) + (((2 * w) & 0xffff0000u) | (uint16_t)(t[w + 1] >> (32 - n - b)));
    return (uint16_t)(t[w] >> (16 - n - b)) & ((1u << n) - 1);
}

uint32_t lex_field18(uint16_t idx) {
    const uint16_t *t = DLLVAR(const uint16_t, 0x63722090);
    uint32_t bit = (uint32_t)idx * 18u;
    uint32_t w = bit >> 4, b = bit & 15;
    uint32_t v = ((uint32_t)t[w] & (0xffffu >> b)) << (b + 2);
    if (b + 18 > 32) return v + ((uint32_t)t[w + 1] << (b - 14)) + ((uint32_t)t[w + 2] >> (30 - b));
    return v + ((uint32_t)t[w + 1] >> (14 - b));
}
