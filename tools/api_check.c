// Drives the library (lib/sapi4tts.h) through a list of jobs in one process - a fresh engine per job,
// or several threads at once - and writes each job's PCM as a WAV for tools/api_check.sh to compare.
//   api_check <dll | -> jobs.txt [threads]      job lines (an empty voice_dir: the voices compiled in): out|voice_dir|mode|flags|pitch|speed|repeat|text
// (pitch / speed -1: leave the defaults; repeat: the text spoken that many times on the same engine,
// the last utterance kept, as the emulator harness's -r). Also prints each mode's limits once:
//   limits <mode> pitch <default> <min> <max> speed <default> <min> <max>
#include "sapi4tts.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Job { char out[512], dir[512], mode[128], text[4096]; unsigned flags; long pitch, speed; int repeat; } Job;
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
    uint32_t bytes = (uint32_t)(n * 2), v;
    uint16_t s;
    fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f);
    s = 2; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    if (n) fwrite(pcm, 2, n, f);
    fclose(f);
}

static int run_job(const Job *j) {
    char err[200];
    s4_engine *e = s4_open(j->dir[0] ? j->dir : NULL, j->mode, err, sizeof err);
    if (!e) { fprintf(stderr, "%s: %s\n", j->mode, err); return -1; }
    if (j->pitch >= 0) s4_set_pitch(e, (unsigned)j->pitch);
    if (j->speed >= 0) s4_set_speed(e, (unsigned)j->speed);
    Buf b = { 0 };
    for (int r = 0; r < (j->repeat > 0 ? j->repeat : 1); r++) {
        b.n = 0;
        if (s4_speak(e, j->text, j->flags, on_pcm, &b) != 0) { fprintf(stderr, "%s: speak failed\n", j->out); return -1; }
    }
    write_wav(j->out, b.pcm, b.n, (uint32_t)s4_sample_rate(e));
    free(b.pcm);
    s4_close(e);
    return 0;
}

static Job *jobs;
static int njobs, nthreads = 1;
static void *worker(void *arg) {
    int t = (int)(intptr_t)arg, fails = 0;
    for (int i = t; i < njobs; i += nthreads) fails += run_job(&jobs[i]) != 0;
    return (void *)(intptr_t)fails;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: api_check <dll> jobs.txt [threads]\n"); return 2; }
    if (argc > 3) nthreads = atoi(argv[3]);
    if (s4_init(strcmp(argv[1], "-") ? argv[1] : NULL) < 0) {    // ("-": the data compiled in)
     fprintf(stderr, "cannot load %s\n", argv[1]); return 2; }
    FILE *f = fopen(argv[2], "r");
    if (!f) { perror(argv[2]); return 2; }
    char line[5000];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        if (!*line) continue;
        jobs = realloc(jobs, (size_t)(njobs + 1) * sizeof *jobs);
        Job *j = &jobs[njobs++];
        memset(j, 0, sizeof *j);
        char *fld[8] = { 0 }, *p = line;
        for (int k = 0; k < 8; k++) {
            fld[k] = p;
            if (k < 7) { p = strchr(p, '|'); if (!p) break; *p++ = 0; }
        }
        snprintf(j->out, sizeof j->out, "%s", fld[0]);
        snprintf(j->dir, sizeof j->dir, "%s", fld[1] ? fld[1] : "");
        snprintf(j->mode, sizeof j->mode, "%s", fld[2] ? fld[2] : "Sam");
        j->flags = fld[3] ? (unsigned)atoi(fld[3]) : 0;
        j->pitch = fld[4] ? atol(fld[4]) : -1;
        j->speed = fld[5] ? atol(fld[5]) : -1;
        j->repeat = fld[6] ? atoi(fld[6]) : 1;
        snprintf(j->text, sizeof j->text, "%s", fld[7] ? fld[7] : "");
    }
    fclose(f);
    // each mode's limits, from a fresh engine
    for (int i = 0; i < njobs; i++) {
        int seen = 0;
        for (int k = 0; k < i; k++) seen |= !strcmp(jobs[k].mode, jobs[i].mode) && !strcmp(jobs[k].dir, jobs[i].dir);
        if (seen) continue;
        s4_engine *e = s4_open(jobs[i].dir[0] ? jobs[i].dir : NULL, jobs[i].mode, NULL, 0);
        if (!e) continue;
        s4_limits l;
        s4_get_limits(e, &l);
        printf("limits %s pitch %u %u %u speed %u %u %u\n", jobs[i].mode, l.pitch_default, l.pitch_min, l.pitch_max,
               l.speed_default, l.speed_min, l.speed_max);
        s4_close(e);
    }
    pthread_t th[16];
    int fails = 0;
    if (nthreads > 16) nthreads = 16;
    for (int t = 0; t < nthreads; t++) pthread_create(&th[t], NULL, worker, (void *)(intptr_t)t);
    for (int t = 0; t < nthreads; t++) {
        void *r;
        pthread_join(th[t], &r);
        fails += (int)(intptr_t)r;
    }
    return fails ? 1 : 0;
}
