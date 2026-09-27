// Voice-mode effects of msttssyn.dll, decompiled (see effect.h).
#include "effect.h"
#include "crt_vc.h"
#include "x87.h"
#include <math.h>
#include <string.h>

LAYOUT(Allpass, fir, 0x1c);
LAYOUT(EffectPreset, warble, 0x14);
LAYOUT(Effect, preset, 0x28);
LAYOUT(Effect, ncoef, 0x38);

double effect_db_to_gain(Effect *self, float db) {
    (void)self;
    if (!((double)db >= -110.0)) return 0.0;   // jb: below, or unordered
    return pow(10.0, (double)db * 0.05);
}

void effect_gain_copy(Effect *self, float *dst, const float *src, uint32_t n, float g) {
    (void)self;
    if (!((double)g > 0.0)) memset(dst, 0, (size_t)n * 4);
    else if ((double)g == 1.0) memcpy(dst, src, (size_t)n * 4);   // 
    else
        for (; n; n--) *dst++ = (float)((double)g * *src++);
}

static inline double clip16(double v) {
    if (!(v >= -32768.0)) return -32768.0;
    if (v > 32767.0) return 32767.0;
    return v;
}

void effect_mix_clip(Effect *self, const float *x, float *y, uint32_t n, float g) {
    (void)self;
    if (!((double)g > 0.0)) return;
    if ((double)g == 1.0) {
        for (; n; n--, x++, y++) *y = (float)clip16((double)*y + *x);
    } else {
        for (; n; n--, x++, y++) *y = (float)clip16((double)g * *y + *x);
    }
}

void effect_allpass(Effect *self, Allpass *ap, uint32_t n, const float *in, float *out) {
    float *end = GP(float, ap->buf) + x87_ftol((double)ap->len * self->shrink);
    for (; n; n--, in++, out++) {
        float d = *GP(float, ap->rd);
        double v = (double)d * ap->gain + *in;
        float vf = (float)v;
        if (v > 0.0) {
            if (!((double)vf >= 0.001)) vf = 0.0f;
        } else {
            if ((double)vf > -0.001) vf = 0.0f;   // (NaN is kept: unordered takes the jbe)
        }
        if (self->ncoef) {
            float *h = GP(float, ap->fir);
            memmove(h + 1, h, (size_t)(self->ncoef * 4 - 4));
            h[0] = vf;
            float *w = GP(float, ap->wr);
            *w = 0.0f;
            const float *cf = GP(const float, self->coef);
            for (int k = 0; k < self->ncoef; k++) *w = (float)((double)h[k] * cf[k] + *w);
        } else {
            *GP(float, ap->wr) = vf;
        }
        *out = (float)((double)d - (double)vf * ap->gain);
        float *w = GP(float, ap->wr) + 1;
        GPSET(ap->wr, w >= end ? GP(float, ap->buf) : w);
        float *r = GP(float, ap->rd) + 1;
        GPSET(ap->rd, r >= end ? GP(float, ap->buf) : r);
    }
}

void effect_chain(Effect *self, float *buf, int n, GPTR(Allpass) *stages) {
    for (int16_t i = 0; i < 5; i++) {
        if (!stages[i]) break;
        effect_allpass(self, GP(Allpass, stages[i]), (uint32_t)n, buf, buf);
    }
}

void effect_process(Effect *self, float *buf, int n) {
    if (self->nstages) {
        self->shrink = (float)(1.0 - (double)GP(const EffectPreset, self->preset)->warble * self->rnd);
        while (n > 0) {
            int k = n < self->chunk ? n : self->chunk;
            effect_gain_copy(self, GP(float, self->scratch), buf, (uint32_t)k, self->send);
            effect_chain(self, GP(float, self->scratch), k, self->stage);
            effect_mix_clip(self, GP(float, self->scratch), buf, (uint32_t)k, self->dry);
            n -= k;
            buf += k;
        }
    }
    self->rnd = (float)((double)vc_rand() * 0.000244140625);
}
