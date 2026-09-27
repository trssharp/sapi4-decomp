// Text to speech with the C alone: the engine's voices enumerated from a directory, one mode set up as
// the original's engine object does it (unit stage, front end, synthesizer on four queues), and each
// text run through the three stages' own item handlers and thread loops, one stage after another,
// the PCM the synthesizer hands to the audio side written to a WAV file. The DLL's data comes from
// the user's copy of msttssyn.dll (or a baked image); the voice files are read from the directory.
//   port_render <dir> [-m mode] [-t] [-l] [-o out.wav] text...
#include "dllimage.h"
#include "voices.h"
#include "voice.h"
#include "unitsel.h"
#include "fe_init.h"
#include "fe_input.h"
#include "fe_phones.h"
#include "queue.h"
#include "crt_vc.h"
#include "vcrt.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DLL_BAKED
extern const DllBlob dll_baked;
#endif

static jmp_buf idle_jmp;
static void idle(void) { longjmp(idle_jmp, 1); }

// a stage's thread loop until it would wait for input that no other thread will now send
// (each with its own rand state, as the engine's threads have)
static void run_stage(int32_t (*loop)(void *), void *obj, uint32_t *rand_state) {
    vc_idle_hook = idle;
    vc_rand_use(rand_state);
    if (!setjmp(idle_jmp)) loop(obj);
    vc_rand_use(NULL);
    vc_idle_hook = NULL;
}
static int32_t unit_loop(void *u) { return unit_thread_loop(u); }
static int32_t voc_loop(void *v) { return voc_thread_loop(v); }

static void write_wav(const char *path, const int16_t *pcm, size_t n, uint32_t rate) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    uint32_t bytes = (uint32_t)(n * 2);
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    uint32_t v = 36 + bytes; memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    uint16_t s = 1; memcpy(h + 20, &s, 2);
    memcpy(h + 22, &s, 2);
    memcpy(h + 24, &rate, 4);
    v = rate * 2; memcpy(h + 28, &v, 4);
    s = 2; memcpy(h + 32, &s, 2);
    s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &bytes, 4);
    fwrite(h, 1, 44, f);
    fwrite(pcm, 2, n, f);
    fclose(f);
}

static void wide_to_char(char *out, size_t cap, const uint16_t *w) {
    size_t k = 0;
    for (; w[k] && k + 1 < cap; k++) out[k] = (char)uni_to_cp1252(w[k]);
    out[k] = 0;
}

int main(int argc, char **argv) {
    const char *mode_name = "Sam", *out = "out.wav";
    int tags = 0, list = 0, a = 1;
    if (argc < 2) {
        fprintf(stderr, "usage: port_render <dir with the voices%s> [-m mode] [-t] [-l] [-o out.wav] text...\n",
#ifdef DLL_BAKED
                ""
#else
                " and msttssyn.dll"
#endif
        );
        return 2;
    }
    const char *dir = argv[a++];
#ifndef DLL_BAKED
    char path[1024];
    snprintf(path, sizeof path, "%s/msttssyn.dll", dir);
    if (!dll_image_load(path)) { fprintf(stderr, "cannot load %s\n", path); return 2; }
#else
    if (!dll_image_use(&dll_baked)) { fprintf(stderr, "cannot set up the image\n"); return 2; }
#endif
    char text[8192] = "";
    for (; a < argc; a++) {
        if (!strcmp(argv[a], "-m") && a + 1 < argc) mode_name = argv[++a];
        else if (!strcmp(argv[a], "-o") && a + 1 < argc) out = argv[++a];
        else if (!strcmp(argv[a], "-t")) tags = 1;
        else if (!strcmp(argv[a], "-l")) list = 1;
        else {
            if (*text) strncat(text, " ", sizeof text - strlen(text) - 1);
            strncat(text, argv[a], sizeof text - strlen(text) - 1);
        }
    }
    // the voices (the engine's enumerator: its three lists)
    ModeList *paths = list_ctor(vc_malloc(sizeof(ModeList))), *hdrs = list_ctor(vc_malloc(sizeof(ModeList))),
             *infos = list_ctor(vc_malloc(sizeof(ModeList)));
    voices_enum(dir, paths, hdrs, infos);
    uint32_t idx = list_count(infos);
    for (uint32_t i = 0; i < list_count(infos); i++) {
        const TtsModeInfo *m = GP(const TtsModeInfo, list_item(infos, i));
        char name[300];
        wide_to_char(name, sizeof name, m->mode_name);
        if (list) printf("%s\n", name);
        if (idx == list_count(infos) && !strcmp(name, mode_name)) idx = i;
    }
    if (list) return 0;
    if (idx == list_count(infos)) { fprintf(stderr, "mode '%s' not found\n", mode_name); return 2; }
    // select it (the engine's Select: the voice table, then the engine object's set-up)
    const VoiceHdr *mode = GP(const VoiceHdr, list_item(hdrs, idx));
    int32_t voice = 0, variant = 0, index = 0;
    if (mode_table_add(mode, paths, hdrs, &voice, &variant, &index) < 0) { fprintf(stderr, "no voice for the mode\n"); return 2; }
    const char *vpath = GP(const char, list_item(paths, (uint32_t)index));
    const uint8_t *fmt = (const uint8_t *)mode + 0x8e0;       // the voice's WAVEFORMATEX
    uint32_t rate;
    memcpy(&rate, fmt + 4, 4);
    Queue *q_text = queue_ctor(vc_malloc(sizeof(Queue))), *q_phones = queue_ctor(vc_malloc(sizeof(Queue))),
          *q_units = queue_ctor(vc_malloc(sizeof(Queue))), *q_audio = queue_ctor(vc_malloc(sizeof(Queue)));
    // (the engine sets high-water marks of 1, 1 and 10 on the last three, to pace its threads; stages
    // run one after another must never find a queue full, so here the marks are out of reach. Setting
    // one also sets the queue's events, which the constructor leaves clear.)
    queue_set_high(q_phones, 0x7fffffff);
    queue_set_high(q_units, 0x7fffffff);
    queue_set_high(q_audio, 0x7fffffff);
    FrontEnd *fe = fe_ctor(vc_calloc(1, sizeof(FrontEnd)));
    UnitStage *u = unit_ctor(vc_calloc(1, sizeof(UnitStage)));
    VocEngine *v = voc_engine_ctor(vc_calloc(1, sizeof(VocEngine)));
    uint16_t *wpath = vc_malloc(2 * (strlen(vpath) + 1));
    vc_MultiByteToWideChar_cp1252((const uint8_t *)vpath, (int)strlen(vpath) + 1, wpath);
    void *stg = NULL;
    if (vc_StgOpenStorage(wpath, 0x20, &stg) < 0) { fprintf(stderr, "cannot open %s\n", vpath); return 2; }
    if (unit_init(u, (uint8_t *)q_phones, (uint8_t *)q_units, stg, (const uint8_t *)mode, voice, variant, 0, 0x46a) < 0 ||
        fe_setup(fe, (uint8_t *)q_text, (uint8_t *)q_phones, (const uint8_t *)mode, voice, variant, (int32_t)(uintptr_t)stg, 0, 0x46a, (int32_t)(uintptr_t)u) < 0 ||
        voc_engine_init(v, (uint8_t *)q_units, (uint8_t *)q_audio, voice, variant, stg, 1, rate, rate,
                        (int32_t)(mode->features & 0x8000), 0, 0x46e) < 0) {
        fprintf(stderr, "engine set-up failed\n");
        return 2;
    }
    vc_com_release(stg);
    // the text, as the front end's thread receives it
    size_t n = strlen(text);
    uint16_t *w = vc_malloc(2 * (n + 1));
    for (size_t i = 0; i <= n; i++) w[i] = cp1252_to_uni[(uint8_t)text[i]];
    FeThreadFrame f;
    memset(&f, 0, sizeof f);
    f.item.code = 1;
    f.item.a.value = (uint32_t)tags;
    f.item.v2 = 4;
    GPSET(f.item.data, (void *)w);
    f.h[0] = fe->abort_event;
    f.h[1] = q_text->ev_nonempty;
    f.h2[0] = fe->abort_event;
    f.h2[1] = q_phones->ev_room;
    fe_text_item(fe, &f);
    if (getenv("PORT_DEBUG")) fprintf(stderr, "after front end: %u phrase items\n", q_phones->count);
    static uint32_t rand_unit = 1, rand_synth = 1;
    run_stage(unit_loop, u, &rand_unit);
    if (getenv("PORT_DEBUG")) fprintf(stderr, "after unit stage: %u unit items (%u left)\n", q_units->count, q_phones->count);
    run_stage(voc_loop, v, &rand_synth);
    if (getenv("PORT_DEBUG")) fprintf(stderr, "after synthesizer: %u audio items (%u left)\n", q_audio->count, q_units->count);
    // the audio side: the PCM records, in order
    int16_t *pcm = NULL;
    size_t np = 0;
    QItem it;
    while (queue_pop(q_audio, &it)) {
        if (it.code == 6 && it.data) {
            pcm = realloc(pcm, (np + it.bytes / 2) * 2);
            memcpy(pcm + np, GP(void, it.data), it.bytes);
            np += it.bytes / 2;
        }
        if (it.data) vc_free(GP(void, it.data));
    }
    write_wav(out, pcm, np, rate);
    fprintf(stderr, "%s: %zu samples at %u Hz\n", out, np, rate);
    return 0;
}

// what the portable build has no C for yet; none of it is reached by the voices this was checked with
int32_t user_lex_load(UserLex *t) { (void)t; return 0; }
int32_t text_from_other(char *text, GPTR(char) *out) { (void)text; (void)out; fprintf(stderr, "text mode not decompiled\n"); abort(); }
void pros_alt_a(Prosody *p) { (void)p; abort(); }
void pros_alt_b(Prosody *p) { (void)p; abort(); }
void pros_alt_c(Prosody *p) { (void)p; abort(); }
void pros_alt_d(GPTR(float) *tmp, Prosody *p) { (void)tmp; (void)p; abort(); }
uint8_t vm_proc_guest(int inst, uint8_t n, int16_t *left, int16_t *len) {
    (void)left; (void)len;
    fprintf(stderr, "rule procedure %d/%d is not decompiled\n", inst, n);
    abort();
}
double lattice_cost_other(uint32_t fn, int32_t a, int32_t b) { (void)fn; (void)a; (void)b; abort(); }
int32_t tree_query(const SenoneTree *t, TreeQuery *q) { (void)t; (void)q; fprintf(stderr, "older senone trees not decompiled\n"); abort(); }
int32_t senone_tree_load_old(void *stg, const char *name, const void *ps, GPTR(SenoneTree) *out) {
    (void)stg; (void)name; (void)ps; (void)out;
    fprintf(stderr, "older senone trees not decompiled\n");
    abort();
}
uint32_t voc_resampler_new(int slot) { (void)slot; fprintf(stderr, "sample-rate converters not decompiled\n"); abort(); }
int32_t voc_resample(uint32_t obj, uint32_t method, float *in, GPTR(float) *out, int32_t n) {
    (void)obj; (void)method; (void)in; (void)out; (void)n;
    abort();
}
void voc_resample_flush(uint32_t obj, uint32_t method) { (void)obj; (void)method; abort(); }
