// Adapters: guest arguments -> the portable C in src/ -> guest results. Pointers are translated to host
// pointers into guest memory (bounds-checked for the extent the function touches).
#include "adapters.h"
#include "dsp.h"
#include <string.h>

#define A(i) x86_arg(c, (i))
#define AI(i) ((int32_t)x86_arg(c, (i)))
static inline float argf(X86 *c, int i) { uint32_t u = x86_arg(c, i); float f; memcpy(&f, &u, 4); return f; }
// host pointer to n elements of guest memory at a (NULL when n <= 0: the function never dereferences it)
#define PTR(T, a, n) ((n) > 0 ? GP(T, (a), (uint32_t)(n) * sizeof(T)) : (T *)NULL)
// host pointer to guest a, checking the element range [lo, hi) the function touches (may be negative)
static void *span(X86 *c, uint32_t a, long lo, long hi, uint32_t es) {
    if (hi <= lo) return c->mem + a;
    uint32_t start = a + (uint32_t)(lo * (long)es);
    x86_ptr(c, start, (uint32_t)((hi - lo) * (long)es));
    return c->mem + a;
}
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void hk_dsp_allpole(Emu *e, X86 *c) {
    int order = AI(1), n = AI(4);
    int m = order > 0 ? order + 1 : 1;
    dsp_allpole(PTR(float, A(0), m), order, PTR(float, A(2), m), PTR(float, A(3), n), n);
}
void hk_dsp_fir(Emu *e, X86 *c) {
    int n = AI(1), order = AI(4);
    int m = order > 0 ? order : 1;
    dsp_fir(PTR(float, A(0), n), n, PTR(float, A(2), m), PTR(float, A(3), m), order);
}
void hk_dsp_iir_c0(Emu *e, X86 *c) {
    int n = AI(1), order = AI(4);
    int m = order > 0 ? order : 1;
    dsp_iir_c0(PTR(float, A(0), n), n, PTR(float, A(2), m), PTR(float, A(3), m), order);
}
void hk_dsp_sum_abs(Emu *e, X86 *c) {
    int n = AI(1);
    x86_fpu_push(c, dsp_sum_abs(PTR(float, A(0), n), n));
}
void hk_dsp_scale(Emu *e, X86 *c) {
    int n = AI(1);
    dsp_scale(PTR(float, A(0), n), n, argf(c, 2));
}
void hk_dsp_reverse(Emu *e, X86 *c) {
    int n = AI(1);
    dsp_reverse(PTR(float, A(0), n), n, PTR(float, A(2), n));
}
void hk_dsp_to_pcm16(Emu *e, X86 *c) {
    int n = AI(2);
    dsp_to_pcm16(PTR(int16_t, A(0), n), PTR(float, A(1), n), n);
}
void hk_dsp_to_pcm8(Emu *e, X86 *c) {
    int n = AI(2);
    dsp_to_pcm8(PTR(uint8_t, A(0), n), PTR(float, A(1), n), n);
}
void hk_dsp_max_abs(Emu *e, X86 *c) {
    int n = AI(1);
    x86_fpu_push(c, dsp_max_abs(PTR(float, A(0), n > 1 ? n : 1), n));
}

void hk_dsp_scale_b(Emu *e, X86 *c) {
    int n = AI(1);
    dsp_scale_b(PTR(float, A(0), n), n, argf(c, 2));
}
void hk_dsp_fold_pulse(Emu *e, X86 *c) {
    int n = AI(1), len = AI(3);
    int m = MIN(len / 2, n);
    float *dst = span(c, A(0), MIN(0, m), MAX(n, 0), 4);
    const float *src = span(c, A(2), 0, MAX(MAX(m, 0), len), 4);
    dsp_fold_pulse(dst, n, src, len);
}
static void halves(X86 *c, int add) {
    int off = AI(2), n = AI(3), len = AI(4);
    long b = (long)len - off - n + 1;
    float *dst = n > 0 ? span(c, A(0), MIN(off, b), MAX(off + n, b + n), 4) : NULL;
    const float *src = n > 0 ? span(c, A(1), 0, 2L * n, 4) : NULL;
    if (add) dsp_add_halves(dst, src, off, n, len); else dsp_place_halves(dst, src, off, n, len);
}
void hk_dsp_place_halves(Emu *e, X86 *c) { halves(c, 0); }
void hk_dsp_add_halves(Emu *e, X86 *c) { halves(c, 1); }
void hk_dsp_cmp_float(Emu *e, X86 *c) {
    E_RET(dsp_cmp_float(GP(float, A(0), 4), GP(float, A(1), 4)));
}
void hk_dsp_sort_floats(Emu *e, X86 *c) {
    int n = AI(1);
    E_RET(dsp_sort_floats(PTR(float, A(0), n), n));
}
