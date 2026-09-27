// Unit differential tests: each decompiled function against the ORIGINAL function in the emulator, on
// random inputs, byte for byte (every output buffer, the return value, x87 results as exact doubles).
// Covers the paths the speech corpus never reaches (8-bit output, filters of unused modes, edge sizes).
//   build/unit data/msttsl [iterations]        exit 0 = every function identical on every case
#include "emu.h"
#include "emu_internal.h"
#include "hooks.h"
#include "dsp.h"
#include "fft.h"
#include "lpc.h"
#include "codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static Emu *E;
static X86 *C;
static EmuModule *M;
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static int rndi(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }
// floats of the kinds the engine sees: small signals, filter coefficients, PCM-scale values, exact
// halves (rounding ties), zeros of both signs, and now and then something huge
static float rndf(void) {
    switch (rnd() % 8) {
    case 0: return (float)((int32_t)rnd() % 70000) + 0.5f;
    case 1: return (rnd() & 1) ? 0.0f : -0.0f;
    case 2: return (float)((int32_t)rnd()) * 1e-3f;
    case 3: return (float)((int32_t)(rnd() % 20001) - 10000) / 7.0f;
    default: return ((float)(int32_t)rnd() / 2147483648.0f) * ((rnd() & 3) ? 1.0f : 2.5f);
    }
}
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static uint32_t gaddr(uint32_t pref) { return pref - MSTTS_PREF_BASE + M->base; }
static uint32_t galloc(uint32_t n) { return emu_malloc(E, n ? n : 4); }
static void *gp(uint32_t a, uint32_t n) { return emu_ptr(E, a, n ? n : 1); }
static uint32_t call(uint32_t pref, int nargs, const uint32_t *args) { return emu_call(E, gaddr(pref), nargs, args); }

static int fails, cases;
static void check(const char *fn, int iter, const char *what, const void *a, const void *b, size_t n) {
    cases++;
    if (memcmp(a, b, n)) {
        if (fails++ < 20) fprintf(stderr, "MISMATCH %s case %d: %s\n", fn, iter, what);
    }
}
static void checkd(const char *fn, int iter, double a, double b) { check(fn, iter, "ST0", &a, &b, 8); }

// a float array in guest memory + a host copy; returns the guest address
static uint32_t farr(float *host, int n) {
    for (int i = 0; i < n; i++) host[i] = rndf();
    uint32_t g = galloc(4u * (uint32_t)n);
    memcpy(gp(g, 4u * (uint32_t)n), host, 4u * (uint32_t)n);
    return g;
}

#define MAXN 700
static float h1[MAXN], h2[MAXN], h3[MAXN];

static void t_filter(int which, int iter) {
    // coefficients small enough to keep the recursion bounded most of the time (overflow compares too)
    int n = rndi(0, 400), order = rndi(0, 24);
    if (iter % 17 == 0) order = rndi(-2, 1);
    int m = order + 2 > 2 ? order + 2 : 2;   // c[0] and mem[0] are read whatever the order
    uint32_t gx = farr(h1, n > 0 ? n : 1), gc = farr(h2, m), gm = farr(h3, m);
    for (int i = 0; i < m; i++) { h2[i] *= 0.3f; ((float *)gp(gc, 4u * m))[i] = h2[i]; }
    uint32_t args[5];
    if (which == 0) { args[0] = gc; args[1] = (uint32_t)order; args[2] = gm; args[3] = gx; args[4] = (uint32_t)n; call(0x636712d5, 5, args); dsp_allpole(h2, order, h3, h1, n); }
    else { args[0] = gx; args[1] = (uint32_t)n; args[2] = gc; args[3] = gm; args[4] = (uint32_t)order;
        if (which == 1) { call(0x63671943, 5, args); dsp_fir(h1, n, h2, h3, order); }
        else { call(0x636719a0, 5, args); dsp_iir_c0(h1, n, h2, h3, order); } }
    const char *nm = which == 0 ? "dsp_allpole" : which == 1 ? "dsp_fir" : "dsp_iir_c0";
    check(nm, iter, "x", gp(gx, 4u * (n > 0 ? n : 1)), h1, 4u * (n > 0 ? n : 0));
    check(nm, iter, "mem", gp(gm, 4u * m), h3, 4u * m);
    check(nm, iter, "coef (unchanged)", gp(gc, 4u * m), h2, 4u * m);
}

static void t_vec(int iter) {
    int n = rndi(0, 600);
    if (iter % 13 == 0) n = rndi(-3, 1);
    int k = n > 0 ? n : 1;
    uint32_t gx = farr(h1, k), gd = farr(h2, k);
    uint32_t a[3];
    // sum_abs, max_abs: ST0
    a[0] = gx; a[1] = (uint32_t)n;
    call(0x63671e49, 2, a); checkd("dsp_sum_abs", iter, x86_fpu_pop(C), dsp_sum_abs(h1, n));
    call(0x63671f31, 2, a); checkd("dsp_max_abs", iter, x86_fpu_pop(C), dsp_max_abs(h1, n));
    // scale
    float g = rndf();
    a[0] = gx; a[1] = (uint32_t)n; a[2] = fbits(g);
    call(0x63671e79, 3, a); dsp_scale(h1, n, g);
    check("dsp_scale", iter, "x", gp(gx, 4u * k), h1, 4u * k);
    // reverse
    a[0] = gx; a[1] = (uint32_t)n; a[2] = gd;
    call(0x63671e96, 3, a); dsp_reverse(h1, n, h2);
    check("dsp_reverse", iter, "dst", gp(gd, 4u * k), h2, 4u * k);
    // to_pcm16 / to_pcm8 (fresh inputs at PCM scale, with ties)
    for (int i = 0; i < k; i++) { h1[i] = (float)((int32_t)(rnd() % 80000) - 40000) / ((rnd() & 1) ? 2.0f : 3.0f); }
    memcpy(gp(gx, 4u * k), h1, 4u * k);
    uint32_t g16 = galloc(2u * k), g8 = galloc((uint32_t)k);
    int16_t o16[MAXN]; uint8_t o8[MAXN];
    memcpy(o16, gp(g16, 2u * k), 2u * k); memcpy(o8, gp(g8, (uint32_t)k), (uint32_t)k);
    a[0] = g16; a[1] = gx; a[2] = (uint32_t)n;
    call(0x63671ebc, 3, a); dsp_to_pcm16(o16, h1, n);
    check("dsp_to_pcm16", iter, "dst", gp(g16, 2u * k), o16, 2u * k);
    a[0] = g8;
    call(0x63671eee, 3, a); dsp_to_pcm8(o8, h1, n);
    check("dsp_to_pcm8", iter, "dst", gp(g8, (uint32_t)k), o8, (uint32_t)k);
}

static void t_more(int iter) {
    uint32_t a[5];
    // fold_pulse: n period, len pulse
    int n = rndi(1, 300), len = rndi(0, 600);
    uint32_t gd = farr(h1, n), gs = farr(h2, len > 0 ? len : 1);
    a[0] = gd; a[1] = (uint32_t)n; a[2] = gs; a[3] = (uint32_t)len;
    call(0x6368864b, 4, a); dsp_fold_pulse(h1, n, h2, len);
    check("dsp_fold_pulse", iter, "dst", gp(gd, 4u * n), h1, 4u * n);
    // scale_b
    float g = rndf();
    a[0] = gd; a[1] = (uint32_t)n; a[2] = fbits(g);
    call(0x636886c6, 3, a); dsp_scale_b(h1, n, g);
    check("dsp_scale_b", iter, "x", gp(gd, 4u * n), h1, 4u * n);
    // place/add halves into a buffer of len2 with off
    int hn = rndi(0, 100), off = rndi(0, 50), len2 = off + 2 * hn + rndi(0, 50);
    int tot = len2 + 2;
    uint32_t gb = farr(h1, tot), gh = farr(h2, 2 * hn + 1);
    a[0] = gb; a[1] = gh; a[2] = (uint32_t)off; a[3] = (uint32_t)hn; a[4] = (uint32_t)len2;
    call(0x6368874c, 5, a); dsp_place_halves(h1, h2, off, hn, len2);
    check("dsp_place_halves", iter, "dst", gp(gb, 4u * tot), h1, 4u * tot);
    call(0x636887a7, 5, a); dsp_add_halves(h1, h2, off, hn, len2);
    check("dsp_add_halves", iter, "dst", gp(gb, 4u * tot), h1, 4u * tot);
    // sort: with duplicates and signed zeros
    int sn = rndi(0, 200);
    for (int i = 0; i < sn; i++) h3[i] = (rnd() & 3) ? (float)(rndi(-20, 20)) * 0.5f : ((rnd() & 1) ? 0.0f : -0.0f);
    if (iter % 5 == 0) for (int i = 0; i < sn; i++) h3[i] = (float)i;   // already sorted
    uint32_t gz = galloc(4u * (sn > 0 ? sn : 1));
    memcpy(gp(gz, 4u * (sn > 0 ? sn : 1)), h3, 4u * (sn > 0 ? sn : 1));
    a[0] = gz; a[1] = (uint32_t)sn;
    uint32_t r1 = call(0x63688708, 2, a);
    int r2 = dsp_sort_floats(h3, sn);
    check("dsp_sort_floats", iter, "ret", &r1, &r2, 4);
    check("dsp_sort_floats", iter, "x", gp(gz, 4u * (sn > 0 ? sn : 1)), h3, 4u * (sn > 0 ? sn : 0));
}

static float fx[4096], fs[8192];
static void t_fft(int iter) {
    int m = rndi(1, 10), n = 1 << m;
    for (int i = 0; i < n; i++) fx[i] = rndf();
    for (int i = 0; i < 2 * n; i++) fs[i] = (iter & 1) ? (float)sin(3.14159265358979 * i / n) : rndf();
    uint32_t gx = galloc(4u * n), gs = galloc(8u * n);
    memcpy(gp(gx, 4u * n), fx, 4u * n);
    memcpy(gp(gs, 8u * n), fs, 8u * n);
    uint32_t a[4] = { gx, (uint32_t)n, (uint32_t)m, gs };
    call(0x63684b60, 4, a);
    fft_inverse_real(fx, n, m, fs);
    check("fft_inverse_real", iter, "x", gp(gx, 4u * n), fx, 4u * n);
}

static void t_lpc(int iter) {
    int order = 2 * rndi(1, 10) + (iter % 7 == 0 ? 1 : 0);
    float l[40], o[41];
    for (int i = 0; i < order; i++) l[i] = (float)(i + 1) / (2.0f * (order + 1)) + ((float)(int)(rnd() % 1000) - 500) * 1e-5f;
    if (iter % 3 == 0) for (int i = 0; i < order; i++) l[i] = (float)(rnd() % 5000) / 10000.0f;   // unsorted, ties
    uint32_t gl = galloc(4u * order), go = galloc(4u * (order + 1));
    memcpy(gp(gl, 4u * order), l, 4u * order);
    uint32_t a[4] = { gl, go, (uint32_t)order, 0 };
    call(0x63678470, 4, a);
    lpc_from_lsf(l, o, order, 0);
    check("lpc_from_lsf", iter, "out", gp(go, 4u * (order + 1)), o, 4u * (order + 1));
    check("lpc_from_lsf", iter, "lsf (sorted in place)", gp(gl, 4u * order), l, 4u * order);
}

static void t_period(int iter) {
    int srcn = rndi(1, 300), dstn = rndi(1, 300), winn = rndi(1, 256);
    if (iter % 4 == 0) dstn = srcn;
    float win[300], s[300], d1[300];
    for (int i = 0; i <= winn; i++) win[i] = (float)i / winn;
    for (int i = 0; i < 300; i++) s[i] = rndf();
    for (int i = 0; i < dstn; i++) d1[i] = rndf();
    uint32_t gs = galloc(4u * 300), gd = galloc(4u * dstn), gw = galloc(4u * (winn + 1));
    memcpy(gp(gs, 1200), s, 1200); memcpy(gp(gd, 4u * dstn), d1, 4u * dstn); memcpy(gp(gw, 4u * (winn + 1)), win, 4u * (winn + 1));
    uint32_t a[8] = { gs, (uint32_t)srcn, gd, (uint32_t)dstn, gw, (uint32_t)winn };
    call(0x6367131b, 6, a); dsp_fit_period(s, srcn, d1, dstn, win, winn);
    check("dsp_fit_period", iter, "dst", gp(gd, 4u * dstn), d1, 4u * dstn);
    float g = (iter % 3 == 0) ? 1.0f : rndf();
    int rev = (iter % 5 == 1);
    a[0] = gs; a[1] = (uint32_t)srcn; a[2] = gd; a[3] = (uint32_t)dstn; a[4] = fbits(g); a[5] = (uint32_t)rev; a[6] = gw; a[7] = (uint32_t)winn;
    if (rev && dstn > 300) rev = 0;
    call(0x636713b8, 8, a); dsp_period(s, srcn, d1, dstn, g, rev, win, winn);
    check("dsp_period", iter, "dst", gp(gd, 4u * dstn), d1, 4u * dstn);
    // byte decoder with escapes
    int8_t b[600]; float o[200];
    int n = rndi(0, 150);
    for (int i = 0; i < 600; i++) { uint32_t r = rnd() % 10; b[i] = r == 0 ? 0x7f : r == 1 ? -128 : (int8_t)rnd(); }
    uint32_t gb = galloc(600), go = galloc(4u * 200);
    memcpy(gp(gb, 600), b, 600);
    int base = rndi(-100, 100);
    a[0] = gb; a[1] = (uint32_t)n; a[2] = go; a[3] = (uint32_t)base;
    uint32_t r1 = call(0x63675c05, 4, a); int r2 = codec_bytes_to_floats(b, n, o, base);
    check("codec_bytes_to_floats", iter, "EAX (bytes consumed)", &r1, &r2, 4);
    check("codec_bytes_to_floats", iter, "out", gp(go, 4u * n), o, 4u * n);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: unit <dir with msttssyn.dll> [iterations]\n"); return 2; }
    int iters = argc > 2 ? atoi(argv[2]) : 300;
    setenv("SAPI4_HOOKS", "none", 1);
    E = emu_create(256u << 20);
    C = emu_cpu(E);
    emu_map_dir(E, "C:\\SPEECH", argv[1]);
    char host[1200];
    if (!emu_host_path(E, "C:\\SPEECH\\msttssyn.dll", host, sizeof host, 1)) { fprintf(stderr, "no msttssyn.dll\n"); return 2; }
    M = emu_load_pe(E, host, "C:\\SPEECH\\msttssyn.dll", EMU_IMAGE_BASE);
    if (!M || !emu_dll_attach(E, M)) { fprintf(stderr, "load failed: %s\n", emu_error(E)); return 2; }
    for (int it = 0; it < iters; it++) {
        t_filter(0, it); t_filter(1, it); t_filter(2, it);
        t_vec(it);
        t_more(it);
        t_fft(it);
        t_lpc(it);
        t_period(it);
        if (emu_failed(E)) { fprintf(stderr, "guest fault: %s\n", emu_error(E)); return 1; }
    }
    printf("unit: %d checks, %d mismatches\n", cases, fails);
    return fails ? 1 : 0;
}
