// The front end without the emulator: text in, the phone lists the unit stage would receive out (as
// the hook build's SAPI4_TRACE_PHONES writes them), the DLL's data loaded from the user's copy of
// msttssyn.dll at run time (or baked in).
//   port_fe <dir with msttssyn.dll> [-t] [-p pitch] [-v f50 flags] < lines
// (-v: a voice variant's number and the character flags the voice data gives it; the voice data itself
// is not ported yet)
#include "dllimage.h"
#include "fe_init.h"
#include "fe_input.h"
#include "fe_phones.h"
#include "queue.h"
#include "crt_vc.h"
#include "vcrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DLL_BAKED
extern const DllBlob dll_baked;
#endif

static Queue *queue_new(void) {
    Queue *q = vc_calloc(1, sizeof(Queue));
    q->high = 0x10000;
    q->ev_nonempty = vc_CreateEventA(1, 0);
    q->ev_full = vc_CreateEventA(1, 0);
    q->ev_empty = vc_CreateEventA(1, 1);
    q->ev_room = vc_CreateEventA(1, 1);
    return q;
}

int main(int argc, char **argv) {
    int tags = 0, pitch = 100, a = 1, f50 = 0;
    unsigned flags = 0;
#ifndef DLL_BAKED
    if (argc < 2) { fprintf(stderr, "usage: port_fe <dir with msttssyn.dll> [-t] [-p pitch] < lines\n"); return 2; }
    char path[1024];
    snprintf(path, sizeof path, "%s/msttssyn.dll", argv[1]);
    if (!dll_image_load(path)) { fprintf(stderr, "cannot load %s\n", path); return 2; }
    a = 2;
#else
    if (!dll_image_use(&dll_baked)) { fprintf(stderr, "cannot set up the image\n"); return 2; }
#endif
    for (; a < argc; a++) {
        if (!strcmp(argv[a], "-t")) tags = 1;
        else if (!strcmp(argv[a], "-p") && a + 1 < argc) pitch = atoi(argv[++a]);
        else if (!strcmp(argv[a], "-v") && a + 2 < argc) {
            f50 = atoi(argv[++a]);
            flags = (unsigned)strtoul(argv[++a], NULL, 16);
        }
    }
    Queue *qin = queue_new(), *qout = queue_new();
    FrontEnd *fe = vc_calloc(1, sizeof(FrontEnd));
    fe_ctor(fe);
    static uint8_t voice[0x8e0];
    *(uint16_t *)(void *)(voice + 0x8d8) = (uint16_t)pitch;
    fe_setup(fe, (uint8_t *)qin, (uint8_t *)qout, voice, 0, 0, 0, 0, 0, 0);
    fe->f50 = f50;          // (fe_setup would look the variant up in the voice data)
    fe->flags = flags;
    printf("setup pitch %d f50 %d flags %x\n", pitch, f50, flags);
    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        size_t n = strlen(line);
        uint16_t *w = vc_malloc(2 * (n + 1));
        for (size_t i = 0; i <= n; i++) w[i] = cp1252_to_uni[(uint8_t)line[i]];
        FeThreadFrame f;
        memset(&f, 0, sizeof f);
        f.item.code = 1;
        f.item.a.value = (uint32_t)tags;
        f.item.v2 = 4;
        GPSET(f.item.data, (void *)w);
        f.h[0] = fe->abort_event;
        f.h[1] = qin->ev_nonempty;
        f.h2[0] = fe->abort_event;
        f.h2[1] = qout->ev_room;
        printf("text tags %d mode 4\n", tags);
        fe_text_item(fe, &f);
        QItem it;
        while (queue_pop(qout, &it)) {
            if (it.code == 3) phones_print(stdout, GP(PhoneIn, it.data), (int32_t)(it.bytes / sizeof(PhoneIn)));
            else fprintf(stderr, "item %u\n", it.code);
            if (it.code == 3) vc_free(GP(void, it.data));
        }
        fflush(stdout);
    }
    return 0;
}

// not decompiled yet: no user lexicon file (as the emulator has none), and the paths a plain text
// never takes
int32_t text_from_other(char *text, GPTR(char) *out) { (void)text; (void)out; fprintf(stderr, "port_fe: text mode not decompiled\n"); abort(); }
uint8_t vm_proc_guest(int inst, uint8_t n, int16_t *left, int16_t *len) {
    (void)left; (void)len;
    fprintf(stderr, "rule procedure %d/%d is not decompiled\n", inst, n);
    abort();
}
double lattice_cost_other(uint32_t fn, int32_t a, int32_t b) { (void)fn; (void)a; (void)b; abort(); }
