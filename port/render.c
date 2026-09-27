// Text to speech with the C alone, as a command: a thin driver over the library (lib/sapi4tts.h).
//   port_render <dir> [-m mode] [-t] [-l] [-p pitch] [-s wpm] [-o out.wav] text...
// <dir> holds msttssyn.dll and the voice files; a build with the DLL's data (-DS4_DLL_BAKED) or the
// voices (-DS4_VOICES_BAKED) compiled in takes them from there instead.
#include "sapi4tts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Buf { int16_t *pcm; size_t n, cap; } Buf;
static int on_pcm(const int16_t *pcm, size_t n, void *user) {
    Buf *b = user;
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 65536;
        b->pcm = realloc(b->pcm, b->cap * sizeof *b->pcm);
    }
    memcpy(b->pcm + b->n, pcm, n * sizeof *pcm);
    b->n += n;
    return 0;
}

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
    if (n) fwrite(pcm, 2, n, f);
    fclose(f);
}

int main(int argc, char **argv) {
    const char *mode_name = "Sam", *out = "out.wav";
    int tags = 0, list = 0, a = 1;
    long pitch = -1, speed = -1;
    if (argc < 2) { fprintf(stderr, "usage: port_render <dir> [-m mode] [-t] [-l] [-p pitch] [-s wpm] [-o out.wav] text...\n"); return 2; }
    const char *dir = argv[a++];
    char text[8192] = "";
    for (; a < argc; a++) {
        if (!strcmp(argv[a], "-m") && a + 1 < argc) mode_name = argv[++a];
        else if (!strcmp(argv[a], "-o") && a + 1 < argc) out = argv[++a];
        else if (!strcmp(argv[a], "-t")) tags = 1;
        else if (!strcmp(argv[a], "-p") && a + 1 < argc) pitch = atol(argv[++a]);
        else if (!strcmp(argv[a], "-s") && a + 1 < argc) speed = atol(argv[++a]);
        else if (!strcmp(argv[a], "-l")) list = 1;
        else {
            if (*text) strncat(text, " ", sizeof text - strlen(text) - 1);
            strncat(text, argv[a], sizeof text - strlen(text) - 1);
        }
    }
#ifdef S4_DLL_BAKED
    const char *dll = NULL;
#else
    char dll[1100];
    snprintf(dll, sizeof dll, "%s/msttssyn.dll", dir);
#endif
#ifdef S4_VOICES_BAKED
    const char *voices = NULL;
#else
    const char *voices = dir;
#endif
    if (s4_init(dll) < 0) { fprintf(stderr, "cannot load the engine's data\n"); return 2; }
    if (list) {
        s4_mode m[64];
        int n = s4_list_modes(voices, m, 64);
        for (int i = 0; i < n && i < 64; i++) printf("%s\n", m[i].name);
        return n < 0;
    }
    char err[200];
    s4_engine *e = s4_open(voices, mode_name, err, sizeof err);
    if (!e) { fprintf(stderr, "%s: %s\n", mode_name, err); return 2; }
    if (pitch >= 0) s4_set_pitch(e, (unsigned)pitch);
    if (speed >= 0) s4_set_speed(e, (unsigned)speed);
    Buf b = { 0 };
    if (s4_speak(e, text, tags ? S4_TAGGED : 0, on_pcm, &b) < 0) { fprintf(stderr, "speak failed\n"); return 1; }
    write_wav(out, b.pcm, b.n, (uint32_t)s4_sample_rate(e));
    fprintf(stderr, "%s: %zu samples at %d Hz\n", out, b.n, s4_sample_rate(e));
    s4_close(e);
    return 0;
}
