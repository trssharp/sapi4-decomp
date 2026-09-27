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
#include "effect.h"
#include "tags.h"
#include "senone.h"
#include "unitsel.h"
#include "fe_rules.h"
#include "voice.h"
#include "fe_vm.h"
#include "fe_lex.h"
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

// Functions on structs with pointer fields: run the original, save the regions it may touch, restore
// them, run the C version on the same guest memory, compare.
typedef struct { uint32_t a, n; uint8_t *save, *orig; } Region;
static Region regs[64];
static int nregs;
static void reg_add(uint32_t a, uint32_t n) { regs[nregs++] = (Region){ a, n, malloc(n), malloc(n) }; memcpy(regs[nregs - 1].orig, gp(a, n), n); }
// (all saved before any is restored: regions may overlap)
static void reg_save(void) {
    for (int i = 0; i < nregs; i++) memcpy(regs[i].save, gp(regs[i].a, regs[i].n), regs[i].n);
    for (int i = nregs - 1; i >= 0; i--) memcpy(gp(regs[i].a, regs[i].n), regs[i].orig, regs[i].n);
}
static void reg_check(const char *fn, int iter) {
    for (int i = 0; i < nregs; i++) {
        char what[32]; snprintf(what, sizeof what, "region %d", i);
        if (getenv("VDBG") && memcmp(regs[i].save, gp(regs[i].a, regs[i].n), regs[i].n))
            for (uint32_t j = 0; j < regs[i].n; j++)
                if (regs[i].save[j] != ((uint8_t *)gp(regs[i].a, regs[i].n))[j])
                    fprintf(stderr, "  region %d +%u (%08x): orig %02x C %02x\n", i, j, regs[i].a + j, regs[i].save[j], ((uint8_t *)gp(regs[i].a, regs[i].n))[j]);
        check(fn, iter, what, regs[i].save, gp(regs[i].a, regs[i].n), regs[i].n);
        free(regs[i].save); free(regs[i].orig);
    }
    nregs = 0;
}
static uint32_t thiscall(uint32_t pref, uint32_t self, int nargs, const uint32_t *args) {
    C->s.r[ECX] = self;
    return call(pref, nargs, args);
}

static void t_effect(int iter) {
    // an Effect and two allpass stages in guest memory, with random state
    uint32_t ge = galloc(sizeof(Effect)), gpre = galloc(sizeof(EffectPreset));
    Effect *ef = gp(ge, sizeof(Effect));
    EffectPreset *pr = gp(gpre, sizeof(EffectPreset));
    memset(ef, 0, sizeof *ef); memset(pr, 0, sizeof *pr);
    pr->warble = (iter & 1) ? 0.4f : 0.0f;
    ef->chunk = rndi(16, 300);
    ef->send = (iter % 4 == 0) ? 1.0f : (iter % 4 == 1) ? 0.0f : 0.7f;
    ef->dry = (iter % 3 == 0) ? 1.0f : 0.6f;
    ef->rnd = (float)(rnd() % 32768) * 0.000244140625f;
    ef->shrink = 1.0f - ((iter & 1) ? 0.4f : 0.0f) * ef->rnd;
    GPSET(ef->preset, gp(gpre, 1));
    int ncoef = (iter % 3 == 2) ? rndi(1, 4) : 0;
    ef->ncoef = ncoef;
    uint32_t gcoef = galloc(4u * 8);
    for (int i = 0; i < 8; i++) ((float *)gp(gcoef, 32))[i] = rndf() * 0.1f;
    ef->coef = gcoef;
    int nst = rndi(0, 5);
    ef->nstages = nst;
    uint32_t gst[5] = { 0 }, gbuf[5] = { 0 }, gfir[5] = { 0 };
    for (int k = 0; k < nst; k++) {
        gst[k] = galloc(sizeof(Allpass));
        Allpass *ap = gp(gst[k], sizeof(Allpass));
        int len = rndi(8, 400);
        gbuf[k] = galloc(4u * len);
        for (int i = 0; i < len; i++) ((float *)gp(gbuf[k], 4u * len))[i] = rndf();
        gfir[k] = galloc(4u * 8);
        for (int i = 0; i < 8; i++) ((float *)gp(gfir[k], 32))[i] = rndf();
        ap->gain = (float)(rnd() % 1000) / 1000.0f;
        ap->len = ap->delay = len;
        ap->buf = gbuf[k]; ap->end = gbuf[k] + 4u * len;
        ap->wr = gbuf[k] + 4u * (uint32_t)rndi(0, len - 1);
        ap->rd = gbuf[k] + 4u * (uint32_t)rndi(0, len - 1);
        ap->fir = gfir[k];
        ef->stage[k] = gst[k];
    }
    int n = rndi(0, 700);
    uint32_t gx = farr(h1, n > 0 ? n : 1);
    for (int i = 0; i < (n > 0 ? n : 1); i++) { h1[i] *= 20000.0f; ((float *)gp(gx, 4))[i] = h1[i]; }
    uint32_t gsc = galloc(4u * 300);
    ef->scratch = gsc;
    // effect_process (the whole chain), then the pieces through their own entry points
    reg_add(ge, sizeof(Effect)); reg_add(gx, 4u * (n > 0 ? n : 1)); reg_add(gsc, 1200);
    for (int k = 0; k < nst; k++) { Allpass *ap = gp(gst[k], sizeof(Allpass)); reg_add(gst[k], sizeof(Allpass)); reg_add(gbuf[k], 4u * ap->len); reg_add(gfir[k], 32); }
    uint32_t seed = E->cur->rand_seed;
    uint32_t a[5] = { gx, (uint32_t)n };
    thiscall(0x63680189, ge, 2, a);
    uint32_t seed_after = E->cur->rand_seed;
    reg_save();
    E->cur->rand_seed = seed;
    effect_process(ef, gp(gx, 4), n);
    check("effect_process", iter, "rand state", &seed_after, &E->cur->rand_seed, 4);
    reg_check("effect_process", iter);
    if (nst > 0) {
        Allpass *ap = gp(gst[0], sizeof(Allpass));
        int m = rndi(0, 500);
        uint32_t gi = farr(h2, m > 0 ? m : 1), go = farr(h3, m > 0 ? m : 1);
        reg_add(gst[0], sizeof(Allpass)); reg_add(gbuf[0], 4u * ap->len); reg_add(gfir[0], 32); reg_add(go, 4u * (m > 0 ? m : 1));
        uint32_t b[4] = { gst[0], (uint32_t)m, gi, go };
        thiscall(0x63680036, ge, 4, b);
        reg_save();
        effect_allpass(ef, ap, (uint32_t)m, gp(gi, 4), gp(go, 4));
        reg_check("effect_allpass", iter);
    }
    // gain_copy, mix_clip, db_to_gain
    {
        int m = rndi(0, 300);
        float g = (iter % 5 == 0) ? 1.0f : (iter % 5 == 1) ? 0.0f : (iter % 5 == 2) ? -1.0f : rndf();
        uint32_t gs = farr(h2, m > 0 ? m : 1), gd = farr(h3, m > 0 ? m : 1);
        for (int i = 0; i < m; i++) { h2[i] *= 30000.0f; ((float *)gp(gs, 4))[i] = h2[i]; }
        reg_add(gd, 4u * (m > 0 ? m : 1));
        uint32_t b[4] = { gd, gs, (uint32_t)m, fbits(g) };
        thiscall(0x6367ff1d, ge, 4, b);
        reg_save();
        effect_gain_copy(ef, gp(gd, 4), gp(gs, 4), (uint32_t)m, g);
        reg_check("effect_gain_copy", iter);
        reg_add(gd, 4u * (m > 0 ? m : 1));
        uint32_t b2[4] = { gs, gd, (uint32_t)m, fbits(g) };
        thiscall(0x6367ff87, ge, 4, b2);
        reg_save();
        effect_mix_clip(ef, gp(gs, 4), gp(gd, 4), (uint32_t)m, g);
        reg_check("effect_mix_clip", iter);
        float db = (float)rndi(-130, 20) + ((iter & 1) ? 0.5f : 0.0f);
        uint32_t b3[1] = { fbits(db) };
        thiscall(0x6367fbfe, ge, 1, b3);
        double g1 = x86_fpu_pop(C), g2 = effect_db_to_gain(ef, db);
        if (memcmp(&g1, &g2, 8)) fprintf(stderr, "db %.9g: guest %.17g native %.17g\n", db, g1, g2);
        checkd("effect_db_to_gain", iter, g1, g2);
    }
}

// whole-heap snapshot, for functions that allocate or free: the original and the C version must leave
// the guest heap (its memory and the allocator state) identical
typedef struct { uint8_t *mem; uint32_t next, n; uint32_t freel[HEAP_CLASSES], large; uint64_t live; } HeapSnap;
static void heap_snap(HeapSnap *h) {
    h->next = E->heap_next; h->n = E->heap_next - EMU_HEAP_BASE;
    h->mem = malloc(h->n); memcpy(h->mem, E->mem + EMU_HEAP_BASE, h->n);
    memcpy(h->freel, E->heap_free, sizeof h->freel); h->large = E->heap_large; h->live = E->heap_live;
}
static void heap_restore(const HeapSnap *h) {
    memcpy(E->mem + EMU_HEAP_BASE, h->mem, h->n);
    E->heap_next = h->next; memcpy(E->heap_free, h->freel, sizeof h->freel); E->heap_large = h->large; E->heap_live = h->live;
}
static void heap_check(const char *fn, int iter, const HeapSnap *a) {
    HeapSnap b; heap_snap(&b);
    check(fn, iter, "heap top", &a->next, &b.next, 4);
    check(fn, iter, "heap free lists", a->freel, b.freel, sizeof a->freel);
    check(fn, iter, "heap large list", &a->large, &b.large, 4);
    check(fn, iter, "heap live", &a->live, &b.live, 8);
    if (a->n == b.n) {
        if (getenv("VDBG") && memcmp(a->mem, b.mem, a->n))
            for (uint32_t i = 0; i < a->n; i += 4)
                if (memcmp(a->mem + i, b.mem + i, 4)) { uint32_t x, y; memcpy(&x, a->mem + i, 4); memcpy(&y, b.mem + i, 4); fprintf(stderr, "  heap %08x: orig %08x C %08x\n", EMU_HEAP_BASE + i, x, y); }
        check(fn, iter, "heap memory", a->mem, b.mem, a->n);
    }
    free(b.mem);
}

// effect_init / effect_free / effect_ctor against the originals, heap included
static void t_effect_life(int iter) {
    int mode = rndi(-1, 7), rate = (iter & 1) ? 22050 : 8000;
    uint32_t ge = galloc(sizeof(Effect));
    memset(gp(ge, sizeof(Effect)), 0, sizeof(Effect));
    C->s.r[ECX] = ge; call(0x6367fee8, 0, NULL);          // original constructor
    HeapSnap h0; heap_snap(&h0);
    uint32_t a[2] = { (uint32_t)mode, (uint32_t)rate };
    uint32_t r1 = thiscall(0x6367fdf5, ge, 2, a) & 0xffff;
    HeapSnap h1; heap_snap(&h1);
    Effect after; memcpy(&after, gp(ge, sizeof(Effect)), sizeof after);
    heap_restore(&h0);
    uint32_t r2 = (uint16_t)effect_init(gp(ge, sizeof(Effect)), mode, rate);
    check("effect_init", iter, "AX", &r1, &r2, 4);
    check("effect_init", iter, "object", &after, gp(ge, sizeof(Effect)), sizeof after);
    heap_check("effect_init", iter, &h1);
    free(h0.mem); free(h1.mem);
    // free it both ways
    heap_snap(&h0);
    thiscall(0x6367fd50, ge, 0, NULL);
    heap_snap(&h1);
    memcpy(&after, gp(ge, sizeof(Effect)), sizeof after);
    heap_restore(&h0);
    effect_free(gp(ge, sizeof(Effect)));
    check("effect_free", iter, "object", &after, gp(ge, sizeof(Effect)), sizeof after);
    heap_check("effect_free", iter, &h1);
    free(h0.mem); free(h1.mem);
    // constructor
    uint32_t g2 = galloc(sizeof(Effect));
    for (uint32_t i = 0; i < sizeof(Effect); i++) ((uint8_t *)gp(g2, sizeof(Effect)))[i] = (uint8_t)rnd();
    Effect e2; memcpy(&e2, gp(g2, sizeof(Effect)), sizeof e2);
    C->s.r[ECX] = g2; uint32_t r3 = call(0x6367fee8, 0, NULL);
    Effect orig; memcpy(&orig, gp(g2, sizeof(Effect)), sizeof orig);
    memcpy(gp(g2, sizeof(Effect)), &e2, sizeof e2);
    uint32_t r4 = (uint32_t)((uint8_t *)effect_ctor(gp(g2, sizeof(Effect))) - C->mem);
    check("effect_ctor", iter, "EAX", &r3, &r4, 4);
    check("effect_ctor", iter, "object", &orig, gp(g2, sizeof(Effect)), sizeof orig);
}

static void rnd_tag(char *t, int max) {
    static const char *kw[] = { "chr", "wrd", "com", "ctx", "emp", "eng", "eow", "fs", "hs", "mrk", "pau", "pit",
                                "prn", "pro", "prt", "rst", "spd", "vce", "vol", "btp", "etp", "tp", "foo", "Spd", "PIT", "x" };
    static const char *bits[] = { "=", "\\", "\"", ",", ":", ";", "<", "{", "}", " ", "12", "007", "-3", "a", "\\\\" };
    int n = 0;
    t[n++] = '\\';
    if (rnd() % 8) n += snprintf(t + n, max - n, "%s", kw[rnd() % 26]);
    int k = rndi(0, 4);
    if (rnd() % 3 == 0) { n += snprintf(t + n, max - n, "=%u\\", rnd() % 100000); k = 0; }
    else if (rnd() % 3 == 0) { n += snprintf(t + n, max - n, "=\"Wh%sis\"\\", (rnd() & 1) ? "\\\\" : ""); k = 0; }
    for (int i = 0; i < k && n < max - 8; i++) n += snprintf(t + n, max - n, "%s", bits[rnd() % 15]);
    if (rnd() % 4) t[n++] = '\\';
    t[n] = 0;
}

static void t_tags(int iter) {
    char tag[200];
    rnd_tag(tag, 150);
    if (tag[1] == '=') tag[1] = 'q';   // "\=" crashes the original (see tag_parse)
    int len = (int)strlen(tag) + 1;
    uint32_t gt = galloc((uint32_t)len), go = galloc(sizeof(TagOut));
    memcpy(gp(gt, (uint32_t)len), tag, (size_t)len);
    memset(gp(go, sizeof(TagOut)), 0, sizeof(TagOut));
    HeapSnap h0; heap_snap(&h0);
    uint32_t a[2] = { gt, go };
    uint32_t r1 = call(0x63681315, 2, a);
    HeapSnap h1; heap_snap(&h1);
    TagOut o1; memcpy(&o1, gp(go, sizeof o1), sizeof o1);
    heap_restore(&h0);
    memcpy(gp(gt, (uint32_t)len), tag, (size_t)len);
    memset(gp(go, sizeof(TagOut)), 0, sizeof(TagOut));
    uint32_t r2 = (uint32_t)tag_parse(gp(gt, (uint32_t)len), gp(go, sizeof(TagOut)));
    check("tag_parse", iter, "EAX", &r1, &r2, 4);
    check("tag_parse", iter, "out", &o1, gp(go, sizeof o1), sizeof o1);
    heap_check("tag_parse", iter, &h1);
    free(h0.mem); free(h1.mem);
    // the lexer on its own over the whole string, token by token, buffer of random size
    int n = rndi(1, 40);
    uint32_t gpp = galloc(4), gb = galloc(64);
    uint32_t toks1[64], toks2[64]; int nt = 0;
    char b1[64 * 64]; 
    wr32(C, gpp, gt);
    memset(gp(gb, 64), 0, 64);
    for (nt = 0; nt < 60; nt++) {
        uint32_t a2[3] = { gpp, gb, (uint32_t)n };
        toks1[nt] = call(0x63680f65, 3, a2);
        memcpy(b1 + 64 * nt, gp(gb, 64), 64);
        if (toks1[nt] == TOK_END || rd32(C, gpp) >= gt + (uint32_t)len) { nt++; break; }
    }
    uint32_t end1 = rd32(C, gpp);
    wr32(C, gpp, gt);
    memset(gp(gb, 64), 0, 64);
    char b2[64];
    for (int i = 0; i < nt; i++) {
        toks2[i] = (uint32_t)tag_lex(gp(gpp, 4), gp(gb, 64), n);
        memcpy(b2, gp(gb, 64), 64);
        check("tag_lex", iter, "buffer", b1 + 64 * i, b2, 64);
    }
    check("tag_lex", iter, "tokens", toks1, toks2, 4u * (uint32_t)nt);
    uint32_t end2 = rd32(C, gpp);
    check("tag_lex", iter, "position", &end1, &end2, 4);
}

// a random senone tree in guest memory, then random lookups (the function is pure)
static void t_senone(int iter) {
    const int np = 12, nm = 4, ns = 3, nn = 40, nq = 30, stride = 4;
    static const char *nm_[12] = { "SIL", "+br", "AA1", "B", "sil", "K", "IY0", "T", "S", "+um", "N", "L" };
    uint32_t gnames = galloc(4u * np);
    for (int i = 0; i < np; i++) { uint32_t g = galloc(8); memcpy(gp(g, 8), nm_[i], strlen(nm_[i]) + 1); wr32(C, gnames + 4u * i, g); }
    uint32_t gps = galloc(sizeof(PhoneSet)); PhoneSet *ps = gp(gps, sizeof *ps); memset(ps, 0, sizeof *ps);
    ps->names = gnames; ps->count = np;
    uint32_t ghdr = galloc(sizeof(SenoneHeader)); SenoneHeader *h = gp(ghdr, sizeof *h); memset(h, 0, sizeof *h);
    h->right_base = 1; h->bit_offset = (iter & 1) ? 3 : 0;
    uint32_t gmodel = galloc(np), gnst = galloc(nm);
    for (int i = 0; i < np; i++) ((int8_t *)gp(gmodel, np))[i] = (int8_t)(rnd() % nm);
    for (int i = 0; i < nm; i++) ((int8_t *)gp(gnst, nm))[i] = (int8_t)rndi(1, ns);
    uint32_t gq = galloc(4u * stride * nq);
    for (int i = 0; i < stride * nq; i++) ((uint32_t *)gp(gq, 4u * stride * nq))[i] = rnd() | rnd();
    uint32_t gql = galloc(2u * 200);
    uint16_t *ql = gp(gql, 400);
    for (int i = 0; i < 200; i++) ql[i] = (rnd() % 4 == 0) ? 0xffff : (uint16_t)(rnd() % nq);
    ql[199] = 0xffff;
    uint32_t groots = galloc(4u * nm);
    for (int m = 0; m < nm; m++) {
        uint32_t gr = galloc(8u * ns);
        for (int st = 0; st < ns; st++) {
            uint32_t gn = galloc(8u * nn);
            TreeNode *nd = gp(gn, 8u * nn);
            for (int k = 0; k < nn; k++) {
                nd[k].qlist = (uint16_t)(rnd() % 190);
                if (k >= nn / 2 || rnd() % 5 == 0) { nd[k].yes = -1; nd[k].no = (int16_t)(rnd() % 5000); }
                else { nd[k].yes = (int16_t)rndi(k + 1, nn - 1); nd[k].no = (int16_t)rndi(k + 1, nn - 1); }
            }
            TreeRoot *r = gp(gr + 8u * st, 8); r->present = (uint16_t)(rnd() % 6 != 0); r->nodes = gn;
        }
        wr32(C, groots + 4u * m, gr);
    }
    uint32_t gt = galloc(sizeof(SenoneTree)); SenoneTree *t = gp(gt, sizeof *t); memset(t, 0, sizeof *t);
    t->hdr = ghdr; t->phones = gps; t->ignore_position = (iter % 5 == 0); t->sil = 0; t->word_end_right = (iter % 3) ? 4 : -1;
    t->flags = (iter & 2) ? 2 : 0; t->nstates = gnst; t->model = gmodel; t->roots = groots; t->qbits = gq; t->qstride = stride; t->qlists = gql;
    static const int posv[] = { 'b', 'B', 'e', 'E', 's', 'S', 0, 'x' };
    for (int k = 0; k < 40; k++) {
        int cur = rndi(-1, np), l = rndi(-1, np), r = rndi(-1, np), pos = posv[rnd() % 8], st = rndi(-1, ns);
        uint32_t a[6] = { gt, (uint32_t)cur, (uint32_t)l, (uint32_t)r, (uint32_t)pos, (uint32_t)st };
        uint32_t r1 = call(0x63674184, 6, a);
        uint32_t r2 = (uint32_t)senone_lookup(t, cur, l, r, pos, st);
        check("senone_lookup", iter, "result", &r1, &r2, 4);
    }
    // phone fix-ups
    static const char *ph[] = { "DX", "AW0", "AY0", "EY0", "OY0", "OY1", "T", "dx", "AW", "" };
    int n = rndi(0, 12);
    uint32_t gph = galloc(100u * (n ? n : 1));
    for (int i = 0; i < n; i++) { memset(gp(gph + 100u * i, 100), 'z', 100); strcpy(gp(gph + 100u * i, 100), ph[rnd() % 10]); }
    char save[1200]; memcpy(save, gp(gph, 100u * (n ? n : 1)), 100u * (n ? n : 1));
    uint32_t a2[2] = { gph, (uint32_t)n }; call(0x636876b2, 2, a2);
    char o1[1200]; memcpy(o1, gp(gph, 100u * (n ? n : 1)), 100u * (n ? n : 1));
    memcpy(gp(gph, 100u * (n ? n : 1)), save, 100u * (n ? n : 1));
    phone_fixups(gp(gph, 100u * (n ? n : 1)), n);
    check("phone_fixups", iter, "names", o1, gp(gph, 100u * (n ? n : 1)), 100u * (n ? n : 1));
}

// unit_prosody_a on random phrases: every field it reads, random; the unit array and the stage compared
static void t_prosody(int iter) {
    int n = rndi(1, 12);
    uint32_t gu = galloc(sizeof(UnitStage)), gr = galloc(sizeof(UnitRec) * (n + 1));
    UnitStage *u = gp(gu, sizeof *u);
    memset(u, 0, sizeof *u);
    u->recs = gr;
    u->sil_phone = 3;
    u->last_b = (iter & 1) ? -1 : rndi(80, 200);
    UnitRec *r = gp(gr, sizeof(UnitRec) * n);
    memset(r, 0, sizeof(UnitRec) * (n + 1));
    for (int i = 0; i < n; i++) {
        r[i].phone = rndi(0, 5);
        r[i].dur = (float)rndi(1, 300) / 7.0f;
        r[i].nstates = rndi(0, 5);
        for (int k = 0; k < r[i].nstates; k++) {
            uint32_t q = rnd() % 6;
            r[i].a[k] = q == 0 ? 0.0f : q == 1 ? 1.0f : (float)(rnd() % 1000) / 1000.0f;
            r[i].b[k] = (rnd() % 3) ? 0.0f : (float)rndi(60, 250) + 0.25f * (float)(rnd() % 4);
            r[i].c[k] = (float)(rnd() % 100);
        }
        if (r[i].nstates && rnd() % 2) r[i].a[0] = 0.0f;
        if (r[i].nstates && rnd() % 2) r[i].a[r[i].nstates - 1] = 1.0f;
    }
    u->last_c = (iter & 2) ? -1.0f : (float)rndi(1, 50) * 0.5f;
    for (int i = 0; i < n; i++) for (int k = 0; k < r[i].nstates; k++) if (rnd() % 3) r[i].c[k] = 0.0f;
    uint32_t a[1] = { (uint32_t)n };
    // pass B runs after pass A in the engine (A inserts the boundary states B relies on): normalise
    // the random phrase with the original A first, on a copy
    uint32_t gr2 = galloc(sizeof(UnitRec) * (n + 1)), gu2 = galloc(sizeof(UnitStage));
    memcpy(gp(gr2, sizeof(UnitRec) * (n + 1)), r, sizeof(UnitRec) * (n + 1));
    memcpy(gp(gu2, sizeof(UnitStage)), u, sizeof(UnitStage));
    ((UnitStage *)gp(gu2, sizeof(UnitStage)))->recs = gr2;
    emu_set_insn_budget(E, 20000000);
    thiscall(0x63686c7d, gu2, 1, a);
    emu_set_insn_budget(E, 0);
    if (emu_failed(E)) { fprintf(stderr, "prosody case %d: original A did not finish\n", iter); exit(1); }
    reg_add(gu2, sizeof(UnitStage)); reg_add(gr2, sizeof(UnitRec) * (n + 1));
    thiscall(0x63686905, gu2, 1, a);
    reg_save();
    unit_prosody_b(gp(gu2, sizeof(UnitStage)), n);
    reg_check("unit_prosody_b", iter);
    // pass A on the untouched random phrase
    reg_add(gu, sizeof(UnitStage)); reg_add(gr, sizeof(UnitRec) * (n + 1));
    emu_set_insn_budget(E, 20000000);
    thiscall(0x63686c7d, gu, 1, a);
    emu_set_insn_budget(E, 0);
    reg_save();
    unit_prosody_a(gp(gu, sizeof(UnitStage)), n);
    reg_check("unit_prosody_a", iter);
}

// rule-module tape helpers: the shared tapes pointed at fresh buffers, every instance tested
static void t_tapes(int iter) {
    static const uint32_t g43[9] = { 0x63697558, 0x636979ca, 0x63698347, 0x636987f0, 0x63698cc8, 0x636991a6, 0x63699615, 0x6369a374, 0x6369a777 };
    static const uint32_t g220[10][2] = { {0x63697583,0x63739780},{0x636979f5,0x637397b8},{0x63697e9c,0x637397f0},{0x63698372,0x63739828},{0x6369881b,0x63739860},
        {0x63698cf3,0x63739898},{0x636991d1,0x637398d0},{0x63699640,0x63739908},{0x6369a39f,0x63739948},{0x6369a7a2,0x63739988} };
    static const uint32_t tapes[4] = { TAPE_A, TAPE_B, TAPE_C, TAPE_D };
    uint32_t save[4];
    for (int k = 0; k < 4; k++) {
        uint32_t t = galloc(400);
        for (int i = 0; i < 400; i++) ((uint8_t *)gp(t, 400))[i] = (rnd() % 8) ? (uint8_t)rndi(0x21, 0x7e) : (uint8_t)rnd();
        save[k] = rd32(C, gaddr(tapes[k])); wr32(C, gaddr(tapes[k]), t);
    }
    int16_t lo = (int16_t)rndi(0, 300), hi = (int16_t)(lo + rndi(-2, 60));
    uint32_t a[2] = { (uint32_t)(int32_t)lo, (uint32_t)(int32_t)hi };
    for (int k = 0; k < 9; k++) {
        uint32_t r1 = call(g43[k], 2, a) & 0xff, r2 = (uint32_t)tapes_graphic(lo, hi);
        check("tapes_graphic", iter, "AL", &r1, &r2, 4);
    }
    for (int k = 0; k < 10; k++) {
        int16_t *ends = gp(gaddr(g220[k][1]), 8);
        int16_t se[4]; memcpy(se, ends, 8);
        for (int q = 0; q < 4; q++) ends[q] = (int16_t)rndi(-1, 300);
        int16_t av = (int16_t)rndi(-5, 330);
        uint32_t b[2] = { (uint32_t)(int32_t)av, 0 };
        for (int q = 0; q < 4; q++) reg_add(rd32(C, gaddr(tapes[q])), 400);
        uint32_t r1 = call(g220[k][0], 2, b);
        reg_save();
        uint32_t r2 = ((uint32_t)(int32_t)av & 0xffff0000u) | (uint16_t)tapes_pad(ends, av);
        check("tapes_pad", iter, "AX", &r1, &r2, 4);
        reg_check("tapes_pad", iter);
        memcpy(ends, se, 8);
    }
    for (int k = 0; k < 4; k++) wr32(C, gaddr(tapes[k]), save[k]);
    // the table searches: real tables, keys taken from them (hits) or perturbed (misses)
    static const uint32_t gb[5][2] = { {0x63697b42,0x636cad90},{0x63697fea,0x636cda80},{0x636984bf,0x636ce848},{0x63699790,0x636d3e78},{0x6369a8f3,0x63718da0} };
    uint8_t *key = gp(gaddr(RULE_KEY), 16);
    uint8_t skey[16]; memcpy(skey, key, 16);
    for (int k = 0; k < 5; k++) {
        uint8_t width = (uint8_t)rndi(1, 6), lo = (uint8_t)rndi(0, 40), hi = (uint8_t)(lo + rndi(0, 40));
        uint32_t base = (uint32_t)rndi(0, 300);
        const uint8_t *tab = gp(gaddr(gb[k][1]), 1);
        uint8_t pick = (uint8_t)rndi(lo, hi);
        memcpy(key, tab + (uint16_t)(width * pick + base), width);
        if (rnd() % 3 == 0) key[rnd() % width] ^= (uint8_t)(1 + rnd() % 255);
        uint32_t a2[4] = { base, lo, hi, width };
        uint32_t r1 = call(gb[k][0], 4, a2) & 0xff, r2 = rule_bsearch(tab, base, lo, hi, width);
        check("rule_bsearch", iter, "AL", &r1, &r2, 4);
    }
    memcpy(key, skey, 16);
}

// the rate routines on random settings (unit_set_rate / reset / amp post a message: compared too,
// through the posted-message queue)
static void t_rate(int iter) {
    uint32_t gu = galloc(sizeof(UnitStage));
    UnitStage *u = gp(gu, sizeof *u);
    memset(u, 0, sizeof *u);
    u->default_rate = (uint32_t)rndi(80, 250);
    u->fast_rate = (uint32_t)rndi(0, 400);
    u->rate = (uint32_t)rndi(0, 600);
    u->alt_rate = (uint32_t)rndi(50, 300);
    u->rate_mode = rndi(0, 1);
    u->rate_flags = rnd() & 3;
    u->dur_scale = (float)rndi(1, 9);
    int which = rndi(0, 3);
    uint32_t arg = (rnd() % 4 == 0) ? 0xffffffffu : (rnd() % 4 == 0) ? 0 : (uint32_t)rndi(1, 800);
    reg_add(gu, sizeof(UnitStage));
    int nq = E->gq.n;
    uint32_t r1 = 0, r2 = 0, a[1] = { arg };
    static const uint32_t fn[4] = { 0x63687e26, 0x63687d4b, 0x63687de5, 0x63687fbb };
    r1 = thiscall(fn[which], gu, which == 1 || which == 3 ? 1 : 0, a);
    int posted1 = E->gq.n - nq;
    reg_save();
    int nq2 = E->gq.n;
    UnitStage *h = gp(gu, sizeof *u);
    switch (which) {
    case 0: unit_rate_update(h); r2 = r1; break;
    case 1: r2 = (uint32_t)unit_set_rate(h, arg); break;
    case 2: r2 = (uint32_t)unit_reset_rate(h); break;
    default: r2 = (uint32_t)unit_set_amp(h, arg); break;
    }
    int posted2 = E->gq.n - nq2;
    check("unit_rate", iter, "EAX", &r1, &r2, 4);
    check("unit_rate", iter, "messages posted", &posted1, &posted2, sizeof posted1);
    reg_check("unit_rate", iter);
    E->gq.n = nq;   // drop the test's messages
}

// the lattice Viterbi on random candidate sets (built with the original constructors), heap compared
static void t_viterbi(int iter) {
    int stages = rndi(1, 8);
    uint32_t ga = galloc(sizeof(VecArray));
    memset(gp(ga, sizeof(VecArray)), 0, sizeof(VecArray));
    C->s.r[ECX] = ga; call(0x636881ed, 0, NULL);
    uint32_t ia[3] = { (uint32_t)stages, 4, 2 };
    thiscall(0x6368824c, ga, 3, ia);
    for (int t = 0; t < stages; t++) {
        int n = rndi(0, 6);
        for (int k = 0; k < n; k++) {
            uint32_t ap[2] = { (uint32_t)t, (uint32_t)((rnd() % 5 == 0) ? 0 : rndi(1, 4)) };
            thiscall(0x636882d6, ga, 2, ap);
        }
    }
    VecArray *va = gp(ga, sizeof(VecArray));
    va->beam = (iter & 1) ? 0.0f : (float)rndi(0, 3);
    if (iter & 2) va->beam = (iter & 4) ? -2.0f : 0.0f;
    HeapSnap h0; heap_snap(&h0);
    VecArray save; memcpy(&save, va, sizeof save);
    // half the cases use a cost function with negative values (a - b), so paths really compete
    static uint32_t fake = 0;
    if (!fake) {
        static const uint8_t code[] = { 0x8B,0x44,0x24,0x04, 0x2B,0x44,0x24,0x08, 0x50, 0xDB,0x04,0x24, 0x58, 0xC3 };
        fake = emu_static(E, sizeof code);
        memcpy(gp(fake, sizeof code), code, sizeof code);
    }
    uint32_t fn = (iter & 2) ? fake : gaddr(0x63685d00);

    uint32_t r1 = thiscall(0x636882fc, ga, 1, &fn);
    VecArray after; memcpy(&after, va, sizeof after);
    HeapSnap h1; heap_snap(&h1);
    heap_restore(&h0);
    memcpy(va, &save, sizeof save);
    uint32_t r2 = (uint32_t)lattice_viterbi(va, fn);
    check("lattice_viterbi", iter, "EAX", &r1, &r2, 4);
    check("lattice_viterbi", iter, "object", &after, va, sizeof after);
    if (getenv("VDBG") && memcmp(&after, va, sizeof after)) {
        fprintf(stderr, "case %d stages %d beam %g: r %u/%u result %08x/%08x cnt %d\n", iter, stages, va->beam, r1, r2, after.result, va->result, va->count);
        for (int t = 0; t < stages; t++) { IntVec *iv = gp(rd32(C, va->items + 4u * t), 16); fprintf(stderr, " stage %d:", t); for (int k = 0; k < iv->count; k++) fprintf(stderr, " %d", ((int32_t *)gp(iv->data, 4))[k]); fprintf(stderr, "\n"); }
    }
    heap_check("lattice_viterbi", iter, &h1);
    free(h0.mem); free(h1.mem);
    // append / lacks on their own
    int16_t arr[8]; for (int i = 0; i < 8; i++) arr[i] = (int16_t)rndi(0, 5);
    uint32_t garr = galloc(16); memcpy(gp(garr, 16), arr, 16);
    int16_t v = (int16_t)rndi(0, 6); int nn = rndi(0, 8);
    uint32_t al[3] = { (uint32_t)(uint16_t)v, (uint32_t)nn, garr };
    uint32_t q1 = call(0x63685bf7, 3, al), q2 = (uint32_t)array_lacks(v, nn, arr);
    check("array_lacks", iter, "EAX", &q1, &q2, 4);
}

// small unit-stage helpers the corpus never reaches
static void t_misc(int iter) {
    // intvec_append / vecarray_append: build the same structure twice, compare with heap
    uint32_t ga = galloc(sizeof(VecArray));
    memset(gp(ga, sizeof(VecArray)), 0, sizeof(VecArray));
    C->s.r[ECX] = ga; call(0x636881ed, 0, NULL);
    int stages = rndi(1, 5);
    uint32_t ia[3] = { (uint32_t)stages, (uint32_t)rndi(0, 3), (uint32_t)rndi(-1, 3) };
    thiscall(0x6368824c, ga, 3, ia);
    HeapSnap h0; heap_snap(&h0);
    uint32_t ops[20][2]; int nops = rndi(1, 20);
    for (int k = 0; k < nops; k++) { ops[k][0] = (uint32_t)rndi(-1, stages - 1); /* (i == count passes the original's check and reads past the array) */ ops[k][1] = (uint32_t)rnd(); }
    uint32_t rs1[20], rs2[20];
    for (int k = 0; k < nops; k++) rs1[k] = thiscall(0x636882d6, ga, 2, ops[k]);
    HeapSnap h1; heap_snap(&h1);
    heap_restore(&h0);
    for (int k = 0; k < nops; k++) rs2[k] = (uint32_t)vecarray_append(gp(ga, sizeof(VecArray)), (int32_t)ops[k][0], (int32_t)ops[k][1]);
    check("vecarray_append", iter, "EAX", rs1, rs2, 4u * nops);
    heap_check("vecarray_append", iter, &h1);
    free(h0.mem); free(h1.mem);
    uint32_t gv = rd32(C, ((VecArray *)gp(ga, sizeof(VecArray)))->items);
    heap_snap(&h0);
    uint32_t x = rnd();
    uint32_t e1 = thiscall(0x636881b0, gv, 1, &x);
    heap_snap(&h1); heap_restore(&h0);
    uint32_t e2 = (uint32_t)intvec_append(gp(gv, sizeof(IntVec)), (int32_t)x);
    check("intvec_append", iter, "EAX", &e1, &e2, 4);
    heap_check("intvec_append", iter, &h1);
    free(h0.mem); free(h1.mem);
    // unit_span_lookup over a sorted table of entries
    int ne = rndi(1, 6);
    uint32_t gt = galloc(4u * (ne + 1));
    int first = 0;
    for (int k = 0; k <= ne; k++) {
        uint32_t ge = galloc(0x2c);
        memset(gp(ge, 0x2c), 0, 0x2c);
        *(int32_t *)gp(ge, 4) = first;
        uint32_t a20 = galloc(2 * 20), a24 = galloc(4 * 20), a28 = galloc(4 * 20);
        for (int q = 0; q < 20; q++) { ((int16_t *)gp(a20, 40))[q] = (int16_t)rnd(); ((uint32_t *)gp(a24, 80))[q] = rnd(); ((uint32_t *)gp(a28, 80))[q] = rnd(); }
        wr32(C, ge + 0x20, a20); wr32(C, ge + 0x24, a24); wr32(C, ge + 0x28, a28);
        wr32(C, gt + 4u * k, ge);
        first += rndi(1, 20);
    }
    int32_t xx = rndi(0, first - 1);
    uint32_t go = galloc(8);
    uint32_t sa[5] = { (uint32_t)xx, (uint32_t)(ne - 1), gt, go, go + 4 };
    uint32_t s1 = call(0x63685d18, 5, sa);
    int32_t o1[2]; memcpy(o1, gp(go, 8), 8);
    int32_t o2[2];
    int32_t last_first; memcpy(&last_first, gp(rd32(C, gt + 4u * (ne - 1)), 4), 4);
    if (xx - last_first < 20) {   // (keep inside the entry's 20 values)
        uint32_t s2 = (uint32_t)unit_span_lookup(xx, ne - 1, gp(gt, 4), &o2[0], &o2[1]);
        check("unit_span_lookup", iter, "EAX", &s1, &s2, 4);
        check("unit_span_lookup", iter, "outputs", o1, o2, 8);
    }
    uint32_t idv = rnd(), i1 = call(0x6367ddfb, 1, &idv), i2 = identity32(idv);
    check("identity32", iter, "EAX", &i1, &i2, 4);
}

// the rule machine's tape routines, every instance, on fresh random tapes
static int vm_skips;
static void t_vm(int iter) {
    static const uint32_t fr[3] = { 0x6368d725, 0x63691765, 0x63694ca9 }, fl[3] = { 0x6368d822, 0x63691862, 0x63694da6 },
        fp[3] = { 0x6368f4b2, 0x6369350d, 0x63696a54 }, fu[3] = { 0x6368f68c, 0x636936e7, 0x63696c2e },
        fs[3] = { 0x6368e4cf, 0x636924fa, 0x63695a41 };
    int k = iter % 3;
    const RuleVM *vm = &rule_vm[k];
    uint32_t save[8], gt[6];
    for (int q = 0; q < 6; q++) {
        gt[q] = galloc(300);
        for (int i = 0; i < 300; i++) ((uint8_t *)gp(gt[q], 300))[i] = (uint8_t)(rnd() % 20);
        save[q] = rd32(C, gaddr(vm->tape[q])); wr32(C, gaddr(vm->tape[q]), gt[q]);
    }
    uint16_t stop; memcpy(&stop, gp(gaddr(vm->top), 2), 2);
    int16_t top = (int16_t)rndi(100, 250); memcpy(gp(gaddr(vm->top), 2), &top, 2);
    #define REGS() do { for (int q = 0; q < 6; q++) reg_add(gt[q], 300); reg_add(gaddr(vm->top), 2); } while (0)
    int16_t lo = (int16_t)rndi(3, 80), hi = (int16_t)(lo + rndi(-3, 60));
    uint32_t a[2] = { (uint32_t)(int32_t)lo, (uint32_t)(int32_t)hi };
    REGS(); call(fr[k], 2, a); reg_save(); vm_shift_right(vm, lo, hi); reg_check("vm_shift_right", iter);
    C->s.r[EAX] = rnd(); uint32_t pe = C->s.r[EAX];
    REGS(); uint32_t e1 = call(fl[k], 2, a); reg_save(); uint32_t e2 = vm_shift_left(vm, lo, hi, pe);
    reg_check("vm_shift_left", iter); check("vm_shift_left", iter, "EAX", &e1, &e2, 4);
    int16_t pa = (int16_t)rndi(3, 60), pb = (int16_t)(pa + rndi(-2, 30));
    uint32_t b[2] = { (uint32_t)(int32_t)pa, (uint32_t)(int32_t)pb };
    REGS(); call(fp[k], 2, b); reg_save(); vm_pop_range(vm, pa, pb); reg_check("vm_pop_range", iter);
    static const uint32_t fm[3] = { 0x6368f567, 0x636935c2, 0x63696b09 };
    uint32_t ma[3] = { (uint32_t)rndi(3, 80), 0, (uint32_t)(int32_t)rndi(-20, 20) };
    ma[1] = ma[0] + (uint32_t)rndi(-3, 50);
    REGS(); uint32_t m1 = call(fm[k], 3, ma); reg_save(); uint32_t m2 = (uint32_t)vm_move(vm, (int32_t)ma[0], (int32_t)ma[1], (int32_t)ma[2]);
    reg_check("vm_move", iter); check("vm_move", iter, "EAX", &m1, &m2, 4);
    // push_until needs a 0x0e below the top
    ((uint8_t *)gp(gt[0], 300))[top - 1 + rndi(0, 20)] = 0x0e;
    uint32_t ps = (uint32_t)rndi(0, 30);
    REGS(); uint32_t u1 = call(fu[k], 1, &ps); reg_save(); uint32_t u2 = (uint32_t)vm_push_until(vm, (int32_t)ps);
    reg_check("vm_push_until", iter); check("vm_push_until", iter, "EAX", &u1, &u2, 4);
    // skip_block over structured bytecode: filler, nested tok,1 ... tok,0 blocks, then tok,end
    uint8_t tok = (iter & 4) ? 0xf5 : (uint8_t)rndi(20, 60), end = (uint8_t)rndi(2, 9);
    uint32_t gcode = galloc(400);
    uint8_t *code = gp(gcode, 400);
    int n = 0, depth = 0;
    while (n < 150) {
        uint32_t r = rnd() % 10;
        if (r == 0 && n < 140) { code[n++] = tok; code[n++] = (tok == 0xf5) ? (uint8_t)rndi(1, 3) : 1; depth++; }
        else if (r == 1 && depth) { code[n++] = tok; code[n++] = 0; code[n++] = (uint8_t)rndi(61, 120); depth--; }
        else code[n++] = (uint8_t)rndi(61, 120);
    }
    while (depth--) { code[n++] = tok; code[n++] = 0; code[n++] = (uint8_t)rndi(61, 120); }
    code[n++] = tok; code[n++] = end; code[n++] = tok; code[n++] = 0;
    uint32_t spc = rd32(C, gaddr(vm->pc));
    wr32(C, gaddr(vm->pc), gcode);
    uint32_t sa[2] = { tok, end };
    reg_add(gaddr(vm->pc), 4);
    emu_set_insn_budget(E, 5000000);
    uint32_t k1 = call(fs[k], 2, sa);
    emu_set_insn_budget(E, 0);
    if (E->failed) { fprintf(stderr, "vm_skip_block case %d: the original did not finish\n", iter); exit(1); }
    {
        reg_save();
        uint32_t k2 = vm_skip_block(vm, tok, end);
        reg_check("vm_skip_block", iter); check("vm_skip_block", iter, "EAX", &k1, &k2, 4);
        vm_skips++;
    }
    wr32(C, gaddr(vm->pc), spc);
    for (int q = 0; q < 6; q++) wr32(C, gaddr(vm->tape[q]), save[q]);
    memcpy(gp(gaddr(vm->top), 2), &stop, 2);
    #undef REGS
}

static void t_cond(int iter) {
    static const uint32_t fn[3] = { 0x6368e11b, 0x63692147, 0x6369568e };
    int k = iter % 3;
    const CondVM *vm = &cond_vm[k];
    uint32_t gtape[3], save_t[3];
    for (int q = 0; q < 3; q++) {
        gtape[q] = galloc(64);
        for (int i = 0; i < 64; i++) ((uint8_t *)gp(gtape[q], 64))[i] = (uint8_t)rndi(0, 0xa1);
        save_t[q] = rd32(C, gaddr(vm->tapes) + 4u * q);
        wr32(C, gaddr(vm->tapes) + 4u * q, gtape[q]);
    }
    uint8_t *vars = gp(gaddr(vm->vars), 16);
    uint8_t svars[16]; memcpy(svars, vars, 16);
    for (int i = 0; i < 16; i++) vars[i] = (rnd() % 3) ? 0 : (uint8_t)rndi(1, 0xa1);
    uint32_t sg[4] = { rd32(C, gaddr(vm->pc)), rd32(C, gaddr(vm->saved)), rd32(C, gaddr(vm->cur)), rd32(C, gaddr(vm->ch)) };
    wr32(C, gaddr(vm->saved), gtape[rnd() % 3]); wr32(C, gaddr(vm->cur), gtape[rnd() % 3]);
    *(uint8_t *)gp(gaddr(vm->ch), 1) = (uint8_t)rndi(0, 0xa1);
    static const uint8_t ops[] = { 0xea, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd };
    uint8_t end = 0xe0;
    uint32_t gc = galloc(64); uint8_t *code = gp(gc, 64);
    int n = rndi(0, 12);
    for (int i = 0; i < n; i++) {
        uint8_t op = ops[rnd() % sizeof ops];
        code[2 * i] = op;
        code[2 * i + 1] = (op == 0xee || op == 0xef || op == 0xf0 || op == 0xf1) ? (uint8_t)rndi(0, 2)
                        : (op == 0xea || op == 0xfd) ? (uint8_t)rndi(0, 15)
                        : (op >= 0xf9) ? (uint8_t)rndi(0, 6) : (uint8_t)rndi(0, 0xa1);
    }
    code[2 * n] = end;
    wr32(C, gaddr(vm->pc), gc);
    int32_t pos = rndi(0, 60);
    reg_add(gaddr(vm->pc), 4); reg_add(gaddr(vm->saved), 4); reg_add(gaddr(vm->cur), 4); reg_add(gaddr(vm->ch), 1);
    reg_add(gaddr(vm->vars), 16); reg_add(gaddr(vm->bound), 2);
    uint32_t a[2] = { end, (uint32_t)pos };
    uint32_t r1 = call(fn[k], 2, a);
    reg_save();
    uint32_t r2 = vm_cond(vm, end, pos);
    check("vm_cond", iter, "EAX", &r1, &r2, 4);
    reg_check("vm_cond", iter);
    memcpy(vars, svars, 16);
    wr32(C, gaddr(vm->pc), sg[0]); wr32(C, gaddr(vm->saved), sg[1]); wr32(C, gaddr(vm->cur), sg[2]); wr32(C, gaddr(vm->ch), sg[3]);
    for (int q = 0; q < 3; q++) wr32(C, gaddr(vm->tapes) + 4u * q, save_t[q]);
}

// the rule matcher on random patterns over random tapes (small alphabets so that patterns match)
static int match_hits;
static void cond_ops(uint8_t *code, int *n, int max) {
    static const uint8_t ops[] = { 0xea, 0xf2, 0xf3, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd };
    int m = rndi(0, max);
    for (int i = 0; i < m; i++) {
        uint8_t op = ops[rnd() % sizeof ops];
        code[(*n)++] = op;
        code[(*n)++] = (op == 0xea || op == 0xfd) ? (uint8_t)rndi(0, 15) : (op >= 0xf9) ? (uint8_t)rndi(0, 6) : (uint8_t)rndi(1, 4);
    }
}
static void t_match(int iter) {
    static const uint32_t fn[3] = { 0x6368d966, 0x63691990, 0x63694ed7 };
    int k = iter % 3;
    const CondVM *vm = &cond_vm[k];
    const MatchVM *mv = &match_vm[k];
    int small = (iter & 2) != 0;
    uint32_t gtape[3], save_t[3];
    for (int q = 0; q < 3; q++) {
        gtape[q] = galloc(256);
        uint8_t *t = gp(gtape[q], 256);
        memset(t, 0x0e, 256);
        for (int i = 1; i < 90; i++)
            t[i] = (rnd() % 6 == 0) ? 0x7e : small ? (uint8_t)rndi(1, 4) : (uint8_t)rndi(0, 0xa1);
        save_t[q] = rd32(C, gaddr(vm->tapes) + 4u * q);
        wr32(C, gaddr(vm->tapes) + 4u * q, gtape[q]);
    }
    uint8_t *vars = gp(gaddr(vm->vars), 16);
    uint8_t svars[16]; memcpy(svars, vars, 16);
    for (int i = 0; i < 16; i++) vars[i] = (rnd() % 3) ? 0 : small ? (uint8_t)rndi(1, 4) : (uint8_t)rndi(1, 0xa1);
    uint32_t g4[] = { vm->pc, vm->saved, vm->cur, mv->saved };
    uint32_t g2[] = { vm->bound, mv->adv, mv->last, mv->maxcnt };
    uint32_t g1[] = { vm->ch, mv->out, mv->clsb };
    uint32_t s4[4], s2[4], s1[3];
    for (int i = 0; i < 4; i++) s4[i] = rd32(C, gaddr(g4[i]));
    for (int i = 0; i < 4; i++) memcpy(&s2[i], gp(gaddr(g2[i]), 2), 2);
    for (int i = 0; i < 3; i++) s1[i] = *(uint8_t *)gp(gaddr(g1[i]), 1);
    wr32(C, gaddr(vm->saved), gtape[rnd() % 3]); wr32(C, gaddr(vm->cur), gtape[rnd() % 3]);
    wr32(C, gaddr(mv->saved), gtape[rnd() % 3]);
    int16_t adv0 = (int16_t)(rnd() % 3 ? 1 : 0); memcpy(gp(gaddr(mv->adv), 2), &adv0, 2);
    uint32_t gc = galloc(256); uint8_t *code = gp(gc, 256);
    int n = 0, nops = rndi(0, 10);
    for (int i = 0; i < nops; i++) {
        switch (rnd() % 12) {
        case 0: code[n++] = small ? (uint8_t)rndi(1, 4) : (uint8_t)rndi(0, 0xe9); break;
        case 1: { static const uint8_t lo[] = { 0xf5, 0xf6, 0xfe, 0xec, 0xeb }; code[n++] = lo[rnd() % 5]; break; }
        case 2: { static const uint8_t sw[] = { 0xee, 0xef, 0xf0, 0xf1 }; code[n++] = sw[rnd() % 4]; code[n++] = (uint8_t)rndi(0, 2); break; }
        case 3: case 4: cond_ops(code, &n, 1); break;
        case 5: code[n++] = 0xf8; code[n++] = (uint8_t)rndi(0, 6); break;
        case 6: code[n++] = 0xf7; cond_ops(code, &n, 3); code[n++] = 0xf7; break;
        case 7: case 8: {
            code[n++] = 0xf4; code[n++] = (uint8_t)rndi(0, 2); code[n++] = (rnd() % 4) ? (uint8_t)rndi(0, 5) : 0xdc;
            cond_ops(code, &n, 2); code[n++] = 0xf4; break;
        }
        default: {
            static const uint8_t ts[] = { 0xf2, 0xf3, 0xf9, 0xfa, 0xfb, 0xfc, 0x00 };
            uint8_t t = ts[rnd() % sizeof ts];
            code[n++] = 0xed; code[n++] = (uint8_t)rndi(0, 2); code[n++] = (rnd() % 4) ? (uint8_t)rndi(0, 5) : 0xdc;
            code[n++] = t; code[n++] = (t == 0xf2 || t == 0xf3) ? (uint8_t)rndi(1, 4) : (uint8_t)rndi(0, 6); code[n++] = 0x55;
        }
        }
    }
    code[n++] = 0xff;
    wr32(C, gaddr(vm->pc), gc);
    int32_t at = rndi(1, 40), back = rndi(0, 40);
    reg_add(gaddr(vm->tapes), 12);
    for (int i = 0; i < 4; i++) reg_add(gaddr(g4[i]), 4);
    for (int i = 0; i < 4; i++) reg_add(gaddr(g2[i]), 2);
    for (int i = 0; i < 3; i++) reg_add(gaddr(g1[i]), 1);
    reg_add(gaddr(vm->vars), 16);
    uint32_t a[2] = { (uint32_t)at, (uint32_t)back };
    emu_set_insn_budget(E, 5000000);
    uint32_t r1 = call(fn[k], 2, a) & 0xffff;
    emu_set_insn_budget(E, 0);
    if (E->failed) { fprintf(stderr, "vm_match case %d: the original did not finish\n", iter); exit(1); }
    reg_save();
    uint32_t r2 = (uint16_t)vm_match(k, at, back);
    check("vm_match", iter, "AX", &r1, &r2, 4);
    reg_check("vm_match", iter);
    if (r1) match_hits++;
    memcpy(vars, svars, 16);
    for (int i = 0; i < 4; i++) wr32(C, gaddr(g4[i]), s4[i]);
    for (int i = 0; i < 4; i++) memcpy(gp(gaddr(g2[i]), 2), &s2[i], 2);
    for (int i = 0; i < 3; i++) *(uint8_t *)gp(gaddr(g1[i]), 1) = (uint8_t)s1[i];
    for (int q = 0; q < 3; q++) wr32(C, gaddr(vm->tapes) + 4u * q, save_t[q]);
}

// rule apply (replacement strings and the 0xfe,1 / 0xfe,2 forms; the procedures run in the corpus)
static void t_apply(int iter) {
    static const uint32_t fn[3] = { 0x6368f187, 0x636931b1, 0x636966f8 };
    static const uint32_t wt[3] = { 0x63738cd4, 0x637393bc, 0x63739584 }, p2g[3] = { 0x63739200, 0x637393d8, 0x637395bc },
        outg[3] = { 0x63738cb0, 0x637393a4, 0x6373956c }, varg[3] = { 0x63738ce0, 0x637393c4, 0x63739590 },
        tabg[3] = { 0x63739204, 0x637393dc, 0x637395bc };
    int k = iter % 3;
    const RuleVM *vm = &rule_vm[k];
    uint32_t save[6], gt[6];
    for (int q = 0; q < 6; q++) {
        gt[q] = galloc(600);
        for (int i = 0; i < 600; i++) ((uint8_t *)gp(gt[q], 600))[i] = (uint8_t)((q == 5) ? rndi(0, 3) : rndi(0x0e, 0xa1));
        save[q] = rd32(C, gaddr(vm->tape[q])); wr32(C, gaddr(vm->tape[q]), gt[q]);
    }
    uint32_t stab[3]; for (int q = 0; q < 3; q++) stab[q] = rd32(C, gaddr(tabg[k]) + 4u * q);
    uint16_t stop; memcpy(&stop, gp(gaddr(vm->top), 2), 2);
    int16_t top = 400; memcpy(gp(gaddr(vm->top), 2), &top, 2);
    uint32_t swt = rd32(C, gaddr(wt[k])), sp2 = rd32(C, gaddr(p2g[k])), spc = rd32(C, gaddr(vm->pc));
    uint8_t sout = *(uint8_t *)gp(gaddr(outg[k]), 1), svars[16]; memcpy(svars, gp(gaddr(varg[k]), 16), 16);
    int nv = run_vm[k].nvars < 7 ? (int)run_vm[k].nvars : 7;   // (B's variables end where its tape 5 pointer is)
    for (int i = 0; i < nv; i++) ((uint8_t *)gp(gaddr(varg[k]), 16))[i] = (uint8_t)rndi(0, 0xa1);
    *(uint8_t *)gp(gaddr(outg[k]), 1) = (uint8_t)rndi(0, 0x19);
    int32_t p2 = rndi(0, 8);
    if (k != 2) wr32(C, gaddr(p2g[k]), (uint32_t)p2);
    else { memcpy(gp(gaddr(p2g[k]), 2), &p2, 2); p2 = (int32_t)rd32(C, gaddr(p2g[k])); }
    uint32_t obj = galloc(0x430), gr = obj + 0x30;   // (counts reach 0x30 + 4 * 0xff)
    uint8_t *o = gp(obj, 0x430);
    for (int i = 0; i < 0x430; i++) o[i] = (uint8_t)rnd();
    int16_t left = (int16_t)rndi(20, 60), len = (int16_t)(left + rndi(0, 30));
    memcpy(o + 0x32, &left, 2); memcpy(o + 0x34, &len, 2);
    o[0x30 + 0x292] = (uint8_t)(rnd() & 1);
    wr32(C, gr + 0x298, gt[rndi(0, 5)]);
    // bytecode: the byte vm_apply steps over, then the output
    uint32_t gc = galloc(64); uint8_t *code = gp(gc, 64);
    int n = 0;
    code[n++] = 0xf3;
    if (iter % 7 == 0) { code[n++] = 0xfe; code[n++] = (uint8_t)rndi(1, 2); }
    else {
        if (rnd() & 1) code[n++] = 0xe9;
        int m = rndi(0, 10);
        for (int i = 0; i < m; i++) {
            static const uint8_t ops[] = { 0xec, 0xee, 0xef, 0xf0, 0xf1, 0xf8, 0xfd, 0x20, 0x41, 0x7e };
            uint8_t op = ops[rnd() % sizeof ops];
            code[n++] = op;
            if (op == 0xef || op == 0xf1) code[n++] = (uint8_t)rndi(0, 2);
            else if (op == 0xee || op == 0xf0) code[n++] = (uint8_t)rnd();   // (an operand byte, ignored)
            else if (op == 0xf8) code[n++] = (uint8_t)rndi(0, 6);
            else if (op == 0xfd) code[n++] = (uint8_t)rndi(0, nv - 1);
        }
        code[n++] = 0xff;
    }
    wr32(C, gaddr(vm->pc), gc);
    for (int q = 0; q < 3; q++) wr32(C, gaddr(tabg[k]) + 4u * q, gt[rndi(0, 5)]);
    if (k == 2) { memcpy(gp(gaddr(p2g[k]), 2), &p2, 2); p2 = (int32_t)rd32(C, gaddr(p2g[k])); }
    int32_t a0 = rndi(0, 39);
    for (int q = 0; q < 6; q++) reg_add(gt[q], 600);
    reg_add(obj, 0x430);
    reg_add(gaddr(wt[k]), 4); reg_add(gaddr(vm->pc), 4); reg_add(gaddr(run_vm[k].abort), 4);
    uint32_t a[2] = { (uint32_t)a0, gr };
    emu_set_insn_budget(E, 5000000);
    uint32_t r1 = call(fn[k], 2, a) & 0xff;
    emu_set_insn_budget(E, 0);
    if (E->failed) {
        fprintf(stderr, "vm_apply case %d (instance %d): the original did not finish (%s); code", iter, k, emu_error(E));
        for (int i = 0; i < n; i++) fprintf(stderr, " %02x", code[i]);
        fprintf(stderr, "\n");
        exit(1);
    }
    reg_save();
    uint32_t r2 = vm_apply(k, a0, gp(gr, sizeof(RuleHdr)));
    check("vm_apply", iter, "AL", &r1, &r2, 4);
    reg_check("vm_apply", iter);
    for (int q = 0; q < 3; q++) wr32(C, gaddr(tabg[k]) + 4u * q, stab[q]);
    wr32(C, gaddr(wt[k]), swt); wr32(C, gaddr(p2g[k]), sp2); wr32(C, gaddr(vm->pc), spc);
    *(uint8_t *)gp(gaddr(outg[k]), 1) = sout; memcpy(gp(gaddr(varg[k]), 16), svars, 16);
    for (int q = 0; q < 6; q++) wr32(C, gaddr(vm->tape[q]), save[q]);
    memcpy(gp(gaddr(vm->top), 2), &stop, 2);
}

// one context opcode of the rule executor, on random state
static void t_exec(int iter) {
    static const uint32_t fn[3] = { 0x6368e53d, 0x63692568, 0x63695aaf }, sv[3] = { 0x637392dc, 0x637394a4, 0x63739684 };
    int k = iter % 3;
    const CondVM *cv = &cond_vm[k];
    const MatchVM *mv = &match_vm[k];
    const RunVM *rv = &run_vm[k];
    int small = (iter & 2) != 0;
    uint32_t gtape[3], save_t[3];
    for (int q = 0; q < 3; q++) {
        gtape[q] = galloc(256);
        uint8_t *t = gp(gtape[q], 256);
        memset(t, 0x0e, 256);
        for (int i = 1; i < 200; i++)
            t[i] = (rnd() % 5 == 0) ? 0x7e : small ? (uint8_t)rndi(1, 4) : (uint8_t)rndi(0, 0xa1);
        save_t[q] = rd32(C, gaddr(cv->tapes) + 4u * q);
        wr32(C, gaddr(cv->tapes) + 4u * q, gtape[q]);
    }
    // snapshot every global the executor (and the condition interpreter it calls) can touch
    uint32_t gl[] = { rv->pc, rv->cur, sv[k], cv->saved, rv->pos, rv->pos0, rv->step, rv->nch, rv->flag, cv->ch,
                      cv->bound, mv->last, mv->maxcnt, mv->clsb };
    uint32_t gs[] = { 4, 4, 4, 4, 2, 2, 2, 2, 1, 1, 2, 2, 2, 1 };
    enum { NG = sizeof gl / sizeof gl[0] };
    uint8_t sg[NG][4];
    for (int i = 0; i < NG; i++) memcpy(sg[i], gp(gaddr(gl[i]), gs[i]), gs[i]);
    uint32_t ar[] = { rv->tries, rv->cpos, rv->cpc, rv->ctape, rv->cpos0, rv->ckind };
    uint32_t as[] = { 8, 16, 32, 32, 16, 16 };
    uint8_t sa[6][32];
    for (int i = 0; i < 6; i++) memcpy(sa[i], gp(gaddr(ar[i]), as[i]), as[i]);
    uint8_t *vars = gp(gaddr(cv->vars), 16);
    uint8_t svars[16]; memcpy(svars, vars, 16);
    int nv = rv->nvars < 7 ? (int)rv->nvars : 7;
    for (int i = 0; i < nv; i++) vars[i] = (rnd() % 3) ? 0 : small ? (uint8_t)rndi(1, 4) : (uint8_t)rndi(1, 0xa1);
    wr32(C, gaddr(rv->cur), gtape[rnd() % 3]); wr32(C, gaddr(sv[k]), gtape[rnd() % 3]); wr32(C, gaddr(cv->saved), gtape[rnd() % 3]);
    int16_t P = (int16_t)rndi(20, 180), A = (int16_t)rndi(-1, 1), S = (rnd() & 1) ? 1 : -1, nch = (int16_t)rndi(0, 3);
    memcpy(gp(gaddr(rv->pos), 2), &P, 2); memcpy(gp(gaddr(rv->pos0), 2), &A, 2);
    memcpy(gp(gaddr(rv->step), 2), &S, 2); memcpy(gp(gaddr(rv->nch), 2), &nch, 2);
    *(uint8_t *)gp(gaddr(rv->flag), 1) = (uint8_t)(rnd() & 1);
    for (int i = 0; i < 6; i++) if (ar[i] != rv->cpc && ar[i] != rv->ctape) for (uint32_t j = 0; j < as[i]; j++) ((uint8_t *)gp(gaddr(ar[i]), as[i]))[j] = (uint8_t)rndi(0, 5);
    uint32_t gc = galloc(256); uint8_t *code = gp(gc, 256);
    for (int j = 0; j < 8; j++) wr32(C, gaddr(rv->cpc) + 4u * j, gc);
    for (int j = 0; j < 8; j++) wr32(C, gaddr(rv->ctape) + 4u * j, gtape[rnd() % 3]);
    int n = 0;
    static const uint8_t ops[] = { 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
                                   0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0x00 };
    uint8_t op = ops[rnd() % sizeof ops];
    if (op == 0) op = small ? (uint8_t)rndi(1, 4) : (uint8_t)rndi(0, 0xe9);
    code[n++] = op;
    switch (op) {
    case 0xea: case 0xfd: code[n++] = (uint8_t)rndi(0, nv - 1); break;
    case 0xee: case 0xf0: code[n++] = (uint8_t)rnd(); break;
    case 0xef: case 0xf1: code[n++] = (uint8_t)rndi(0, 2); break;
    case 0xf2: case 0xf3: code[n++] = small ? (uint8_t)rndi(1, 4) : (uint8_t)rndi(0, 0xa1); break;
    case 0xf9: case 0xfa: case 0xfb: case 0xfc: code[n++] = (uint8_t)rndi(0, 6); break;
    case 0xf5: case 0xf6: {
        code[n++] = (uint8_t)rndi(0, 3);
        int m = rndi(0, 6);
        for (int i = 0; i < m; i++) code[n++] = (uint8_t)rndi(0x20, 0x60);
        code[n++] = op; code[n++] = 0; code[n++] = 0x33;
        break;
    }
    case 0xf7:
        code[n++] = (uint8_t)rndi(0, 3); code[n++] = (uint8_t)rndi(0, 3);
        cond_ops(code, &n, 3); code[n++] = 0xf7; break;
    case 0xf4:
        code[n++] = (uint8_t)rndi(0, 2); code[n++] = (rnd() % 4) ? (uint8_t)rndi(0, 5) : 0xdc;
        cond_ops(code, &n, 2); code[n++] = 0xf4; break;
    case 0xed: {
        static const uint8_t ts[] = { 0xf2, 0xf3, 0xf9, 0xfa, 0xfb, 0xfc, 0x00 };
        uint8_t t = ts[rnd() % sizeof ts];
        code[n++] = (uint8_t)rndi(0, 2); code[n++] = (rnd() % 4) ? (uint8_t)rndi(0, 5) : 0xdc;
        code[n++] = t; code[n++] = (t == 0xf2 || t == 0xf3) ? (small ? (uint8_t)rndi(1, 4) : (uint8_t)rndi(0, 0xa1)) : (uint8_t)rndi(0, 6);
        break;
    }
    }
    code[n++] = 0xff;
    wr32(C, gaddr(rv->pc), gc);
    for (int i = 0; i < NG; i++) reg_add(gaddr(gl[i]), gs[i]);
    for (int i = 0; i < 6; i++) reg_add(gaddr(ar[i]), as[i]);
    reg_add(gaddr(cv->vars), 16);
    emu_set_insn_budget(E, 5000000);
    uint32_t r1 = call(fn[k], 0, NULL) & 0xff;
    emu_set_insn_budget(E, 0);
    if (E->failed) {
        fprintf(stderr, "vm_exec case %d (instance %d): the original did not finish (%s); code", iter, k, emu_error(E));
        for (int i = 0; i < n; i++) fprintf(stderr, " %02x", code[i]);
        fprintf(stderr, "\n");
        exit(1);
    }
    reg_save();
    uint32_t r2 = vm_exec(k);
    check("vm_exec", iter, "AL", &r1, &r2, 4);
    reg_check("vm_exec", iter);
    memcpy(vars, svars, 16);
    for (int i = 0; i < NG; i++) memcpy(gp(gaddr(gl[i]), gs[i]), sg[i], gs[i]);
    for (int i = 0; i < 6; i++) memcpy(gp(gaddr(ar[i]), as[i]), sa[i], as[i]);
    for (int q = 0; q < 3; q++) wr32(C, gaddr(cv->tapes) + 4u * q, save_t[q]);
}

// run an original that may not finish on random input: on the watchdog, put the CPU back and report 0
static int skipped;
static int call_bounded(uint32_t pref, int nargs, const uint32_t *args, uint32_t *ret) {
    X86State st = C->s;
    int stop = C->stop;
    emu_set_insn_budget(E, 50000000);
    *ret = call(pref, nargs, args);
    emu_set_insn_budget(E, 0);
    if (!E->failed) return 1;
    if (!strstr(emu_error(E), "watchdog")) { fprintf(stderr, "guest fault: %s\n", emu_error(E)); exit(1); }
    C->s = st;
    C->stop = stop;
    C->faulted = 0;
    E->failed = 0;
    emu_clear_abort(E);
    skipped++;
    return 0;
}

// a rule set run over a random segment, with the real rules (and, to reach the rule types the rule data
// never uses, rules whose type is patched in the emulator's copy of .rdata); everything the rules and
// their procedures can write is compared: .data, the tapes, the heap
static int main_applied, main_runs;
static void t_main(int iter) {
    static const uint32_t fn[3] = { 0x6368c997, 0x636909b7, 0x63693ef8 };
    static const uint32_t firsts[3] = { 0x636a07be, 0x636a32a6, 0x636a7c46 }, lasts[3] = { 0x636a07de, 0x636a32c6, 0x636a7cc6 },
        offs[3] = { 0x636a1ee6, 0x636a523e, 0x636abee6 }, codes[3] = { 0x636a2517, 0x636a5e9f, 0x636af29f };
    int k = iter % 3;
    const RuleVM *vm = &rule_vm[k];
    int mode, f, l;
    do {
        mode = rndi(0, 15);
        f = *(int16_t *)gp(gaddr(firsts[k] + 2u * mode), 2);
        l = *(int16_t *)gp(gaddr(lasts[k] + 2u * mode), 2);
    } while (!(f >= 1 && l >= f && l < 400));
    // the tapes as the engine lays them out (one block; 0x63693792 and its copies set this up), and the
    // 1-based array the tape-select opcodes index: tapes[1..5] = tape 0..4, minus one
    static const uint32_t toff[6] = { 0x585, 0xb05, 0x1085, 0x1605, 0x438e, 5 };
    static const uint32_t tsel[3] = { 0x63739204, 0x637393dc, 0x637395bc };
    uint32_t blk = galloc(0x5000);
    uint8_t *b = gp(blk, 0x5000);
    for (int i = 0; i < 0x5000; i++) b[i] = (uint8_t)((rnd() % 3) ? rndi(0x41, 0x5a) : rndi(0x0f, 0xae));
    for (int i = 5; i < 0x585; i++) b[i] = (uint8_t)rndi(0, 2);
    memset(b, 0, 5);
    for (int q = 0; q < 6; q++) {
        wr32(C, gaddr(vm->tape[q]), blk + toff[q]);
        if (q < 5) wr32(C, gaddr(tsel[k]) + 4u * (q + 1), blk + toff[q] - 1);
    }
    int16_t top = 0x501; memcpy(gp(gaddr(vm->top), 2), &top, 2);
    wr32(C, gaddr(run_vm[k].abort), 0);
    if (k == 0) wr32(C, gaddr(0x63739274), blk);        // the word object the tapes are part of
    int32_t sidx = rndi(20, 80);
    uint32_t gpend = galloc(4);
    int16_t pend = (int16_t)(sidx + rndi(1, 30));
    memcpy(gp(gpend, 2), &pend, 2);
    // patch one rule of the set to a rule type the data does not use (its group: itself and what follows)
    uint8_t *tp = NULL, tsave = 0;
    if (iter % 4 == 0) {
        int i = rndi(f, l);
        int16_t off = *(int16_t *)gp(gaddr(offs[k] + 2u * i), 2);
        tp = gp(gaddr(codes[k]) + (uint32_t)off, 1);
        tsave = *tp;
        static const uint8_t ty[] = { 2, 6, 9, 10, 11, 12, 14, 15, 16 };
        *tp = ty[rnd() % sizeof ty];
    }
    // the stack under the caller: the same garbage for both runs
    uint32_t sp = C->s.r[ESP];
    memset(gp(sp - 0x8000, 0x8000), 0x5a, 0x8000);
    const uint32_t dbase = 0x6371c000, dsize = 0x1e0b0;
    uint8_t *d0 = malloc(dsize); memcpy(d0, gp(gaddr(dbase), dsize), dsize);
    HeapSnap h0; heap_snap(&h0);
    uint32_t a[3] = { (uint32_t)mode, (uint32_t)sidx, gpend };
    uint32_t r1;
    if (!call_bounded(fn[k], 3, a, &r1)) {
        heap_restore(&h0); memcpy(gp(gaddr(dbase), dsize), d0, dsize);
        if (tp) *tp = tsave;
        free(d0); free(h0.mem);
        return;
    }
    r1 &= 0xff;
    HeapSnap h1; heap_snap(&h1);
    uint8_t *d1 = malloc(dsize); memcpy(d1, gp(gaddr(dbase), dsize), dsize);
    heap_restore(&h0);
    memcpy(gp(gaddr(dbase), dsize), d0, dsize);
    memset(gp(sp - 0x8000, 0x8000), 0x5a, 0x8000);
    uint32_t r2 = vm_main(k, mode, sidx, gp(gpend, 2)) & 0xff;
    if (emu_failed(E)) { fprintf(stderr, "vm_main case %d: guest fault in the C run (%s)\n", iter, emu_error(E)); exit(1); }
    check("vm_main", iter, "AL", &r1, &r2, 4);
    if (getenv("VDBG") && memcmp(d1, gp(gaddr(dbase), dsize), dsize))
        for (uint32_t i = 0; i < dsize; i++) if (d1[i] != ((uint8_t *)gp(gaddr(dbase), dsize))[i]) fprintf(stderr, "  .data %08x: orig %02x C %02x\n", dbase + i, d1[i], ((uint8_t *)gp(gaddr(dbase), dsize))[i]);
    check("vm_main", iter, ".data", d1, gp(gaddr(dbase), dsize), dsize);
    heap_check("vm_main", iter, &h1);
    main_runs++; main_applied += r1 != 0;
    if (tp) *tp = tsave;
    free(d0); free(d1); free(h0.mem); free(h1.mem);
}

// the decompiled rule procedures (the wrappers run whole rule sets) against the originals
static int proc_runs;
static void t_proc(int iter) {
    static const uint32_t tsel[3] = { 0x63739204, 0x637393dc, 0x637395bc };
    static const uint32_t tabs[3] = { 0x63721cd0, 0x63721f30, 0x63721f80 };
    static const uint32_t toff[6] = { 0x585, 0xb05, 0x1085, 0x1605, 0x438e, 5 };
    int k = iter % 3, n;
    uint32_t fnp;
    for (;;) {
        n = rndi(3, k == 2 ? 61 : 16);
        fnp = rd32(C, gaddr(tabs[k]) + 4u * (uint32_t)n) - M->base + MSTTS_PREF_BASE;
        extern int vm_proc_is_c(int inst, uint8_t n);
        if (vm_proc_is_c(k, (uint8_t)n)) break;
    }
    const RuleVM *vm = &rule_vm[k];
    uint32_t blk = galloc(0x5000);
    uint8_t *b = gp(blk, 0x5000);
    static const uint8_t spice[] = { 0x22, 0x27, 0x60, 0x47, 0x2d, 0x7e, 0x7e, 0x7e };
    for (int i = 0; i < 0x5000; i++) b[i] = (uint8_t)((rnd() % 4) ? rndi(0x41, 0x5a) : (rnd() & 1) ? spice[rnd() % 8] : rndi(0x0f, 0xae));
    for (int i = 5; i < 0x585; i++) b[i] = (uint8_t)rndi(0, 2);
    memset(b, 0, 5);
    for (int q = 0; q < 6; q++) {
        wr32(C, gaddr(vm->tape[q]), blk + toff[q]);
        if (q < 5) wr32(C, gaddr(tsel[k]) + 4u * (q + 1), blk + toff[q] - 1);
    }
    int16_t top = 0x501; memcpy(gp(gaddr(vm->top), 2), &top, 2);
    wr32(C, gaddr(run_vm[k].abort), 0);
    if (k == 0) wr32(C, gaddr(0x63739274), blk);        // the word object the tapes are part of
    if (k == 0) wr32(C, gaddr(0x63738c98), rnd() & 1);
    uint32_t gl = galloc(8);
    int16_t left = (int16_t)rndi(20, 80), len = (int16_t)(left + rndi(-1, 30));
    memcpy(gp(gl, 2), &left, 2); memcpy(gp(gl + 4, 2), &len, 2);
    uint32_t sp = C->s.r[ESP];
    memset(gp(sp - 0x8000, 0x8000), 0x5a, 0x8000);
    const uint32_t dbase = 0x6371c000, dsize = 0x1e0b0;
    uint8_t *d0 = malloc(dsize); memcpy(d0, gp(gaddr(dbase), dsize), dsize);
    HeapSnap h0; heap_snap(&h0);
    uint8_t blk0[0x5000]; memcpy(blk0, b, 0x5000);
    uint32_t a[2] = { gl, gl + 4 };
    uint32_t r1;
    if (!call_bounded(fnp, 2, a, &r1)) {
        heap_restore(&h0); memcpy(gp(gaddr(dbase), dsize), d0, dsize);
        free(d0); free(h0.mem);
        return;
    }
    r1 &= 0xff;
    HeapSnap h1; heap_snap(&h1);
    uint8_t *d1 = malloc(dsize); memcpy(d1, gp(gaddr(dbase), dsize), dsize);
    uint8_t blk1[0x5000]; memcpy(blk1, b, 0x5000);
    uint8_t lr1[8]; memcpy(lr1, gp(gl, 8), 8);
    heap_restore(&h0);
    memcpy(gp(gaddr(dbase), dsize), d0, dsize);
    memcpy(b, blk0, 0x5000);
    memcpy(gp(gl, 2), &left, 2); memcpy(gp(gl + 4, 2), &len, 2);
    memset(gp(sp - 0x8000, 0x8000), 0x5a, 0x8000);
    uint32_t r2 = vm_callback(k, (uint8_t)n, gp(gl, 2), gp(gl + 4, 2));
    char what[40]; snprintf(what, sizeof what, "vm_proc %d/%d", k, n);
    check(what, iter, "AL", &r1, &r2, 4);
    check(what, iter, "tapes", blk1, b, 0x5000);
    check(what, iter, "left/len", lr1, gp(gl, 8), 8);
    check(what, iter, ".data", d1, gp(gaddr(dbase), dsize), dsize);
    heap_check(what, iter, &h1);
    proc_runs++;
    free(d0); free(d1); free(h0.mem); free(h1.mem);
}

// the lexicon helpers: key packing and the n-bit field arrays
static void t_lex(int iter) {
    uint8_t *codes = gp(gaddr(LEX_CODES), 64);
    uint8_t bits = (iter & 1) ? 5 : 6;
    for (int i = 0; i < 64; i++) codes[i] = (uint8_t)rndi(0, (1 << bits) - 1);
    uint16_t cnt = (uint16_t)rndi(1, 15);
    reg_add(gaddr(LEX_KEY), 32);
    uint32_t a[2] = { bits, cnt };
    uint32_t r1 = call(0x6369a9c8, 2, a);
    reg_save();
    uint32_t r2 = lex_pack(bits, cnt);
    check("lex_pack", iter, "EAX", &r1, &r2, 4);
    reg_check("lex_pack", iter);
    static const struct { uint32_t fn, table; int n; } fs[] = {
        { 0x6369a87e, 0x6371a748, 12 }, { 0x636992ad, 0x636d33f8, 12 }, { 0x63698dcf, 0x636d1bd8, 12 },
        { 0x636988f7, 0x636cffe0, 11 }, { 0x6369844e, 0x636cefd0, 11 }, { 0x63697f78, 0x636cdba8, 9 },
        { 0x63697ad1, 0x636cd1d0, 13 }, { 0x6369971c, 0x636d4078, 10 } };
    for (int f = 0; f < 8; f++) {
        uint32_t idx = (uint32_t)rndi(0, 2500);
        uint32_t e1 = call(fs[f].fn, 1, &idx), e2 = lex_field(fs[f].table, (uint16_t)idx, fs[f].n);
        check("lex_field", iter, "EAX", &e1, &e2, 4);
    }
    uint32_t idx = (uint32_t)rndi(0, 30000);
    uint32_t e1 = call(0x6369a47b, 1, &idx), e2 = lex_field18((uint16_t)idx);
    check("lex_field18", iter, "EAX", &e1, &e2, 4);
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
    decomp_guest_mem = C->mem;
    decomp_load_delta = M->base - MSTTS_PREF_BASE;
    decomp_emu = E;
    for (int it = 0; it < iters; it++) {
        t_filter(0, it); t_filter(1, it); t_filter(2, it);
        t_vec(it);
        t_more(it);
        t_fft(it);
        t_lpc(it);
        t_period(it);
        t_effect(it);
        t_tags(it);
        t_senone(it);
        t_prosody(it);
        t_tapes(it);
        t_rate(it);
        t_misc(it);
        t_vm(it);
        t_cond(it);
        t_match(it);
        t_apply(it);
        t_exec(it);
        t_lex(it);
        if (it < 600) t_main(it);
        if (it < 900) t_proc(it);
        if (it < 500) t_viterbi(it);
        if (it < 300) t_effect_life(it);
        if (emu_failed(E)) { fprintf(stderr, "guest fault: %s\n", emu_error(E)); return 1; }
    }
    printf("unit: %d checks, %d mismatches (vm_skip_block compared on %d cases, vm_match matched in %d, vm_main applied rules in %d of %d, %d rule procedures, %d random cases skipped: the original did not finish)\n", cases, fails, vm_skips, match_hits, main_applied, main_runs, proc_runs, skipped);
    return fails ? 1 : 0;
}
