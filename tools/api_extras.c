// The library's calls that have no counterpart in the golden set: stop from another thread, reset,
// pitch / speed clamping. Each check prints one line; exit status 1 if any fails.
//   api_extras <dll> <voice_dir> [baked]
#include "sapi4tts.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
static void ok(int cond, const char *what) {
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) fails++;
}

typedef struct Count { size_t n; s4_engine *e; int stop_after; } Count;
static int count_pcm(const int16_t *pcm, size_t n, void *user) {
    (void)pcm;
    Count *c = user;
    c->n += n;
    if (c->stop_after && c->n > 0) s4_stop(c->e);   // (the one call allowed from the callback)
    return 0;
}
static int ask_stop(const int16_t *pcm, size_t n, void *user) {
    (void)pcm;
    ((Count *)user)->n += n;
    return 1;
}

typedef struct Th { s4_engine *e; int r; Count c; } Th;
static void *speaker(void *arg) {
    Th *t = arg;
    t->r = s4_speak(t->e, "It was the best of times, it was the worst of times, it was the age of wisdom, it was the age "
                          "of foolishness. It was the epoch of belief, it was the epoch of incredulity. It was the season "
                          "of Light, it was the season of Darkness.", 0, count_pcm, &t->c);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: api_extras <dll> <voice_dir>\n"); return 2; }
    ok(s4_init(argv[1]) == 0, "init");
    ok(s4_init(argv[1]) == 0, "init again");
    s4_mode modes[64];
    int n = s4_list_modes(argv[2], modes, 64);
    ok(n >= 19, "list modes");
    s4_engine *e = s4_open(argv[2], "Sam", NULL, 0);
    ok(e != NULL, "open Sam");
    ok(s4_open(argv[2], "No Such Voice", NULL, 0) == NULL, "open an unknown mode fails");
    s4_limits l;
    s4_get_limits(e, &l);
    ok(s4_set_pitch(e, 1) == 0 && s4_get_pitch(e) == l.pitch_min, "pitch below the range is clamped to the minimum");
    ok(s4_set_pitch(e, 100000) == 0 && s4_get_pitch(e) == l.pitch_max, "pitch above the range is clamped to the maximum");
    ok(s4_set_speed(e, 1) == 0 && s4_get_speed(e) == l.speed_min, "speed clamped to the minimum");
    ok(s4_set_speed(e, 100000) == 0 && s4_get_speed(e) == l.speed_max, "speed clamped to the maximum");
    ok(s4_reset(e) == 0 && s4_get_pitch(e) == l.pitch_default && s4_get_speed(e) == l.speed_default, "reset restores pitch and speed");
    // stop requested by the callback's return value
    Count c = { 0, e, 0 };
    int r = s4_speak(e, "One. Two. Three. Four. Five. Six.", 0, ask_stop, &c);
    ok(r == 1 && c.n > 0, "a callback returning nonzero stops the utterance");
    // stop from another thread (the callback also stops: whichever comes first)
    Th t = { e, -1, { 0, e, 0 } };
    pthread_t th;
    pthread_create(&th, NULL, speaker, &t);
    for (int k = 0; k < 2000 && t.c.n == 0; k++) usleep(1000);
    s4_stop(e);
    pthread_join(th, NULL);
    ok(t.r == 1 || t.r == 0, "stop from another thread returns");
    Count full = { 0, e, 0 };
    s4_engine *e2 = s4_open(argv[2], "Sam", NULL, 0);
    s4_speak(e2, "It was the best of times, it was the worst of times, it was the age of wisdom, it was the age "
                 "of foolishness. It was the epoch of belief, it was the epoch of incredulity. It was the season "
                 "of Light, it was the season of Darkness.", 0, count_pcm, &full);
    printf("(stopped at %zu of %zu samples, result %d)\n", t.c.n, full.n, t.r);
    ok(t.r == 1 && t.c.n < full.n, "the stopped utterance is shorter than the whole");
    // after a stop, the engine speaks on
    Count again = { 0, e, 0 };
    ok(s4_speak(e, "Still here.", 0, count_pcm, &again) == 0 && again.n > 0, "speaks again after a stop");
    s4_stop(e);         // (nothing in progress: forgotten)
    Count again2 = { 0, e, 0 };
    ok(s4_speak(e, "And again.", 0, count_pcm, &again2) == 0 && again2.n > 0, "a stop while idle is forgotten");
    s4_close(e2);
    s4_close(e);
    // many engines, opened and closed
    int good = 1;
    for (int k = 0; k < 40 && good; k++) {
        s4_engine *x = s4_open(argv[2], modes[k % n].name, NULL, 0);
        good = x != NULL;
        s4_close(x);
    }
    ok(good, "40 engines opened and closed in turn");
    // the limit: opening fails cleanly once the engine's table of word objects is used up
    char err[200] = "";
    int opened = 0;
    s4_engine *x;
    while ((x = s4_open(argv[2], "Sam", err, sizeof err)) != NULL && opened < 200) {
        opened++;
        s4_close(x);
    }
    printf("(%d more engines, then: %s)\n", opened, err);
    ok(x == NULL && err[0], "opening past the process's limit fails with a message");
    return fails ? 1 : 0;
}
