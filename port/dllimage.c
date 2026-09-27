// The portable build's view of msttssyn.dll's data: the sections of the user's copy of the DLL,
// mapped at their offsets, read at run time (nothing of Microsoft's is compiled in). A DLL address is
// the original's preferred-base address. Pointer-holding dwords (globals in .data, tables in .rdata)
// get a table of host pointers, one slot per aligned dword, initialised from the base relocations.
#include "dllimage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREF_BASE 0x63670000u

static uint8_t *image;
static uint32_t image_size;
static void **ptr_slots;

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

int dll_image_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc((size_t)n);
    if (!file || fread(file, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(file); return 0; }
    fclose(f);
    uint32_t pe = rd32(file + 0x3c);
    if (memcmp(file + pe, "PE\0\0", 4)) { free(file); return 0; }
    const uint8_t *coff = file + pe + 4, *opt = coff + 20;
    uint32_t nsec = rd16(coff + 2), optsize = rd16(coff + 16);
    uint32_t base = rd32(opt + 28);
    if (base != PREF_BASE) { free(file); return 0; }
    image_size = rd32(opt + 56);
    image = calloc(1, image_size);
    const uint8_t *sec = opt + optsize;
    for (uint32_t i = 0; i < nsec; i++, sec += 40) {
        uint32_t vsize = rd32(sec + 8), va = rd32(sec + 12), rsize = rd32(sec + 16), raw = rd32(sec + 20);
        uint32_t len = rsize < vsize ? rsize : vsize;
        if (va + len <= image_size && raw + len <= (uint32_t)n) memcpy(image + va, file + raw, len);
    }
    // the pointer slots: every relocated (aligned) dword becomes a host pointer into the image
    ptr_slots = calloc(image_size / 4 + 1, sizeof(void *));
    uint32_t rel_va = rd32(opt + 96 + 5 * 8), rel_size = rd32(opt + 96 + 5 * 8 + 4);
    for (uint32_t off = 0; off + 8 <= rel_size;) {
        const uint8_t *blk = image + rel_va + off;
        uint32_t page = rd32(blk), size = rd32(blk + 4);
        if (size < 8) break;
        for (uint32_t e = 8; e + 2 <= size; e += 2) {
            uint32_t ent = rd16(blk + e);
            if ((ent >> 12) != 3) continue;
            uint32_t at = PREF_BASE + page + (ent & 0xfff);
            if (((at - PREF_BASE) & 3) == 0 && at - PREF_BASE < image_size) {
                uint32_t v = rd32(image + (at - PREF_BASE));
                if (v >= PREF_BASE && v < PREF_BASE + image_size) ptr_slots[(at - PREF_BASE) / 4] = image + (v - PREF_BASE);
            }
        }
        off += size;
    }
    free(file);
    return 1;
}

void *decomp_dll_data(uint32_t addr) {
    if (addr < PREF_BASE || addr - PREF_BASE >= image_size) { fprintf(stderr, "dll_data: %08x outside the image\n", addr); abort(); }
    return image + (addr - PREF_BASE);
}

void **decomp_dll_ptr(uint32_t addr) {
    if (addr < PREF_BASE || addr - PREF_BASE >= image_size || ((addr - PREF_BASE) & 3)) { fprintf(stderr, "dll_ptr: %08x not a dword of the image\n", addr); abort(); }
    return &ptr_slots[(addr - PREF_BASE) / 4];
}
