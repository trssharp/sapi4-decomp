// A first portable driver: look words up in the main lexicon with the decompiled C alone (no
// emulator), the DLL's data loaded from the user's copy of msttssyn.dll at run time.
//   port_lex <dir with msttssyn.dll> < words
#include "dllimage.h"
#include "lexdump.h"
#include "fe_vm.h"
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: port_lex <dir with msttssyn.dll> < words\n"); return 2; }
    char path[1024];
    snprintf(path, sizeof path, "%s/msttssyn.dll", argv[1]);
    if (!dll_image_load(path)) { fprintf(stderr, "cannot load %s\n", path); return 2; }
    static uint8_t blk[0x5000];
    const RuleVM *vm = &rule_vm[2];
    char word[256];
    while (fgets(word, sizeof word, stdin)) {
        word[strcspn(word, "\r\n")] = 0;
        int n = (int)strlen(word);
        if (!n || n > 60) continue;
        memset(blk, 'q', sizeof blk);
        for (int q = 0; q < 6; q++) *DLLPTR(char, vm->tape[q]) = (char *)blk + lexdump_toff[q];
        int16_t left = 20;
        memcpy(blk + lexdump_toff[0] + left, word, (size_t)n);
        int16_t l1 = (int16_t)(left + 1), len = (int16_t)(left + n);
        uint8_t found = vm_callback(2, 22, &l1, &len);
        lexdump_print(stdout, word, found, blk, (int16_t)(left + 1), len);
    }
    return 0;
}

// the procedures the portable build does not have yet: none of machine C's lexicon path calls them
uint8_t vm_proc_guest(int inst, uint8_t n, int16_t *left, int16_t *len) {
    fprintf(stderr, "rule procedure %d/%d is not decompiled\n", inst, n);
    abort();
}
