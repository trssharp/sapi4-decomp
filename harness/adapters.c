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
