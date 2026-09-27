// Signal-processing leaves of msttssyn.dll, decompiled. Each keeps the original's order of loads,
// stores and x87 roundings (see x87.h), so the output is bit-identical and in-place/aliasing calls
// behave the same.
#include "dsp.h"
#include "x87.h"

void dsp_allpole(const float *a, int order, float *mem, float *x, int n) {
    for (; n > 0; n--, x++) {
        mem[0] = *x;
        for (int k = order; k > 0; k--) {
            double p = (double)a[k] * mem[k];
            mem[0] = (float)((double)mem[0] - p);
            mem[k] = mem[k - 1];
        }
        *x = mem[0];
    }
}

void dsp_fir(float *x, int n, const float *c, float *mem, int order) {
    for (; n > 0; n--, x++) {
        float xi = *x;
        mem[0] = xi;
        double acc = (double)xi * c[0];
        for (int k = order - 1; k > 0; k--) {
            float prev = mem[k - 1];
            double p = (double)c[k] * mem[k];
            mem[k] = prev;
            acc = acc + p;
        }
        *x = (float)acc;
    }
}

void dsp_iir_c0(float *x, int n, const float *c, float *mem, int order) {
    for (; n > 0; n--, x++) {
        double acc = (double)c[0] * *x;
        for (int k = order - 1; k > 0; k--) {
            float m = mem[k - 1];
            mem[k] = m;
            double p = (double)c[k] * m;
            acc = p + acc;
        }
        *x = (float)acc;
        mem[0] = (float)acc;
    }
}

double dsp_sum_abs(const float *x, int n) {
    double acc = 0.0;
    for (; n > 0; n--, x++) acc = acc + x87_abs_cmp0(*x);
    return acc;
}

void dsp_scale(float *x, int n, float g) {
    for (; n > 0; n--, x++) *x = (float)((double)g * *x);
}

void dsp_reverse(const float *src, int n, float *dst) {
    const float *s = src + n - 1;
    for (; n > 0; n--) *dst++ = *s--;
}

void dsp_to_pcm16(int16_t *dst, const float *src, int n) {
    for (; n > 0; n--) *dst++ = (int16_t)x87_fistp32((double)*src++);
}

void dsp_to_pcm8(uint8_t *dst, const float *src, int n) {
    for (int i = 0; i < n; i++) {
        int32_t v = x87_ftol(floor((double)src[i] - -0.5));
        dst[i] = (uint8_t)((v >> 8) ^ 0x80);
    }
}

double dsp_max_abs(const float *x, int n) {
    double m = x87_abs_cmp0(x[0]);
    for (int i = 1; i < n; i++) {
        double a = x87_abs_cmp0(x[i]);
        if (!(m >= a)) m = a;
    }
    return m;
}
