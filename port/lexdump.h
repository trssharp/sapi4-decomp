// One word through the main lexicon (rule procedure 22 of machine C): shared by the portable driver
// (port/lex.c) and the emulator reference (harness/unit.c --lexdump), which must print the same.
#pragma once
#include <stdio.h>
#include <string.h>
#include <stdint.h>
// the tape layout in the word object (the offsets 0x63693792 and its copies use)
static const uint32_t lexdump_toff[6] = { 0x585, 0xb05, 0x1085, 0x1605, 0x438e, 5 };
static void lexdump_print(FILE *out, const char *word, uint8_t found, const uint8_t *blk, int16_t left, int16_t len) {
    fprintf(out, "%s\t%d\t%d\t", word, found, len);
    for (int t = 0; t < 4; t++) {
        fputc(t ? '|' : '[', out);
        for (int i = left - 1; i < left + 40; i++) {
            uint8_t c = blk[lexdump_toff[t] + i];
            if (c >= 0x21 && c < 0x7f && c != '|' && c != '\\') fputc(c, out);
            else fprintf(out, "\\%02x", c);
        }
    }
    fputs("]\n", out);
}
